#pragma once

// =============================================================================
// ThreadManager / ManagedThread —— 全项目的线程唯一出口
//
// 目的：线程不再由各层自己 `new std::thread`，而是统一从这里取。
//   1. 有**全局上限**（concurrency.maxThreads，默认 16），超了就拒绝并打 ERROR，
//      而不是让某个循环无限起线程把盒子拖死；
//   2. 有**登记表**，`snapshot()` 能随时列出"现在活着哪些线程"，排查卡死时第一个要看的东西；
//   3. 线程体统一包了 try/catch —— 任何一个循环里抛异常只死这一个线程，
//      不会 std::terminate() 把整个进程带走（这正是洗车现场"莫名重启"的常见根因）；
//   4. 线程名带业务含义，`top -H` / 日志里的线程号能对上人话。
//
// 为什么不用"固定 N 个 worker 的线程池"：
//   本项目的线程是**长驻阻塞循环**（采集、两个泵、上报调度、采样），
//   一个 worker 被一个循环占住就再也腾不出来。若池子比循环数少，直接死锁；
//   若池子等于循环数，那就退化成"一对一"，只是多了一层任务队列的壳。
//   真正需要防的是"线程无节制增长"，所以这里做的是**有上限的登记 + 拒绝**，
//   语义比线程池更贴合，出错时行为也更好预测。
//
// 用法：
//   ManagedThread t;
//   t.start("acquisition", [this]{ loop(); });   // false = 超上限/已在跑，调用方降级
//   ...
//   t.join();                                    // 或析构时自动 join
// =============================================================================

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ThreadManager {
public:
    static ThreadManager& instance();

    /// 全局上限。由 Common::init 从 concurrency.maxThreads 读入，默认 16。
    void setLimit(int n);
    int  limit() const;

    /// 当前活着的线程数（线程体返回即减，不等 join）
    int count() const;

    /// "acquisition(1) uplink(1) ..." 形式的一行摘要，启动/退出日志用
    std::string snapshot() const;

    /// 拒绝次数（超上限被挡下来的次数），用于判断预算是否配小了
    int rejectedCount() const;

    // --- 仅供 ManagedThread 调用 ---
    bool acquire(const std::string& name);
    void release(const std::string& name);

private:
    ThreadManager() = default;

    mutable std::mutex m_;
    std::vector<std::string> live_;
    size_t limit_{16};
    size_t rejected_{0};
};

// -----------------------------------------------------------------------------

/// 一个受管的线程。语义与 std::thread 基本一致，但：
///   · start(name, fn) 走上限检查，超过预算返回 false 而不是抛异常
///   · 线程体统一 try/catch，异常只记日志不传播
///   · 线程函数返回时自动从登记表销号（不等 join），count() 始终反映真实存活数
class ManagedThread {
public:
    ManagedThread() = default;
    ~ManagedThread();

    ManagedThread(ManagedThread&&) noexcept;
    ManagedThread& operator=(ManagedThread&&) noexcept;
    ManagedThread(const ManagedThread&) = delete;
    ManagedThread& operator=(const ManagedThread&) = delete;

    /// 起线程。失败（超预算 / 已经在跑 / name 为空）返回 false，调用方自己决定降级策略。
    bool start(const std::string& name, std::function<void()> fn);

    void join();
    bool joinable() const;

    const std::string& name() const { return name_; }

private:
    void detach_state();

    std::string name_;
    std::thread thread_;
};
