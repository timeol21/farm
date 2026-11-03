#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 任务执行方式:
//   INLINE —— 跑在调度线程里, 只放毫秒级判定(上报检查 / AI 派发 / 命令超时)
//   WORKER —— 每个任务一个线程, 每周期调一次, 放阻塞 IO(PLC 采集 / 洗车进度轮询)
// 之所以分两档: 采集一轮要几百毫秒(串口), 和判定任务挤一个线程会互相拖。
enum class TimerTaskMode { INLINE, WORKER };

// 定时器 = 任务注册中心 + 统一调度 + 统一启停。
// 线程所有权在这里: 服务方只注册"每 N 毫秒做一次什么", 不再自己起循环线程。
class ITimer {
public:
    virtual ~ITimer() = default;

    // 同名任务重复注册 = 覆盖(改周期/改回调), 不会叠加
    virtual void addTask(const std::string& name, int intervalMs, TimerTaskMode mode,
                         std::function<void()> task) = 0;
    virtual void removeTask(const std::string& name) = 0;
    virtual void setInterval(const std::string& name, int intervalMs) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual std::size_t taskCount() const = 0;
};

class Timer : public ITimer {
public:
    Timer() = default;
    ~Timer() override;

    void addTask(const std::string& name, int intervalMs, TimerTaskMode mode,
                 std::function<void()> task) override;
    void removeTask(const std::string& name) override;
    void setInterval(const std::string& name, int intervalMs) override;

    void start() override;
    void stop() override;
    bool isRunning() const override;
    std::size_t taskCount() const override;

private:
    struct Task;

    void schedulerLoop();
    void workerLoop(const std::shared_ptr<Task>& task);
    static std::int64_t nowMs();

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<Task>> m_tasks;
    // 从任务自己的线程里 removeTask 时不能 join 自己, 先挪到这里, 统一在 stop/析构时收
    std::vector<std::shared_ptr<Task>> m_retired;
    std::atomic<bool> m_running{false};
    std::thread m_scheduler;
};
