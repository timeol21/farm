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

    virtual void log(LogLevel level, const std::string& message) = 0; //什么样的日志级别，什么样的日志消息

    // virtual void setLogLevel(LogLevel level) = 0;   
};


class AsyncLogger : public ILogger{
public :
    AsyncLogger(std::shared_ptr<ILogQueue> queue);

    ~AsyncLogger();

    void log(LogLevel level, const std::string& message) override;

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

// 日志宏定义（简化日志调用）
#define LOG_DEBUG(msg) LoggerManager::instance().getLogger("work")->log(LogLevel::DEBUG, msg)
#define LOG_INFO(msg) LoggerManager::instance().getLogger("work")->log(LogLevel::INFO, msg)
#define LOG_WARNING(msg) LoggerManager::instance().getLogger("work")->log(LogLevel::WARNING, msg)
#define LOG_ERROR(msg) LoggerManager::instance().getLogger("work")->log(LogLevel::ERROR, msg)


#define COMMAND_LOG_DEBUG(msg) LoggerManager::instance().getLogger("command_work")->log(LogLevel::DEBUG, msg)
#define COMMAND_LOG_INFO(msg) LoggerManager::instance().getLogger("command_work")->log(LogLevel::INFO, msg)
#define COMMAND_LOG_WARNING(msg) LoggerManager::instance().getLogger("command_work")->log(LogLevel::WARNING, msg)
#define COMMAND_LOG_ERROR(msg) LoggerManager::instance().getLogger("command_work")->log(LogLevel::ERROR, msg)


#endif