#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <ctime>
#include <atomic>
#include "common/log/log_queue.h"
#include "common/log/log_sink.h"
class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, const std::string& message) = 0;

    virtual void setLogLevel(LogLevel level) = 0;
};


class AsyncLogger : public ILogger {
public:
    AsyncLogger(std::shared_ptr<ILogQueue> queue);

    ~AsyncLogger();

    void log(LogLevel level, const std::string& message) override;

    void setLogLevel(LogLevel level) override;

    // 添加输出目标（未来支持多个）
    void addSink(std::shared_ptr<ILogSink> sink);

    // 启动 / 停止
    bool start();
    void stop();

private:
    void worker();  // 后台线程

private:
    std::shared_ptr<ILogQueue> m_queue;

    std::vector<std::shared_ptr<ILogSink>> m_sinks;

    std::thread m_workerThread;
    std::atomic<bool> m_running{false};

    LogLevel m_level = LogLevel::INFO;
};


// 注意：LOG_* 宏定义在 log_manager.h 里。所有用宏的文件只需 include "common/log/log_manager.h"。

#endif