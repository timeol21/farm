#pragma once

// =============================================================================
// LogSystem —— 日志装配的唯一入口
//
// 由 Common（common/common.h）按顺序调用，业务代码不直接用它：
//   Common::init()  -> LogSystem::init()
//   Common::start() -> LogSystem::start()
//   Common::stop()  -> LogSystem::stop()
//
//   init()  —— 建日志目录 -> 造 FileSink + ConsoleSink -> 造 AsyncLogger ->
//              设级别 -> 注册到 LoggerManager（此时不开线程，消息先入队）
//   start() —— 启动日志工作线程，之后队列里的日志才开始真正落盘；
//              顺手把当前生效配置 dump 一遍
//   stop()  —— 停线程，退出前把队列里剩下的日志刷完（不然最后几条会丢）
//
// 日志放在哪里：
//   **固定为 <程序目录>/data/log**，不由 config.json 指定，也不再接受 box.logPath。
//   理由：日志目录是"程序自己住哪儿"的推论，不是运维参数。让日志系统自己决定，
//   就不存在"路径写错导致一条日志都看不到"这种最难排查的故障。
//
// 级别 / 保留天数 / 队列容量仍来自配置（这三项确实需要现场调）：
//   box.logLevel                -> debug / info / warning / error（认不出来按 info）
//   box.logKeepDays             -> 保留最近几天
//   queueCapacity.logCapacity   -> 日志队列上限
//
// 为什么 init 和 start 要分开：
//   建目录、开文件属于"初始化"，失败要当场报出来；
//   后台线程属于"启动"，main 想在哪一刻开始记日志就在哪一刻调。
//   合在一起会出现"初始化成功了但线程起不来"这种混在一起的状态。
// =============================================================================

#include <memory>
#include <string>

class AsyncLogger;

class LogSystem {
public:
    static LogSystem& instance();

    /// 建目录 + sink + logger 装配。不开线程。
    bool init();

    /// 启动日志工作线程，并把生效配置 dump 到日志。
    bool start();

    /// 停止线程并 flush。
    void stop();

    /// 日志目录的绝对路径（init 之后有效），形如 <程序目录>/data/log
    static const std::string& dir();

    bool inited() const { return inited_; }
    bool started() const { return started_; }

private:
    LogSystem() = default;
    LogSystem(const LogSystem&) = delete;
    LogSystem& operator=(const LogSystem&) = delete;

    std::shared_ptr<AsyncLogger> logger_;
    bool inited_{false};
    bool started_{false};
};
