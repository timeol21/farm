#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>

// log.h 定义了 ILogger / AsyncLogger / LogLevel。
// 我们 include 它是因为 LOG_* 宏展开后需要这些类型的完整定义（->log() 和 LogLevel::WARNING 等）。
// log.h 自身不反向 include 本文件，所以没有循环。
#include "common/log/log.h"

class LoggerManager {
public:
    static LoggerManager& instance();

    std::shared_ptr<ILogger> getLogger(const std::string& name);

    void registerLogger(const std::string& name,std::shared_ptr<ILogger> logger);

    void log(const std::string& name, LogLevel level, const std::string& message);


private:
    LoggerManager() = default;

private:
    std::unordered_map<std::string, std::shared_ptr<ILogger>> m_loggers;
    mutable std::shared_mutex m_mutex; // ⭐ 新增
};

// 日志宏：本头文件自包含，所有 include "common/log/log_manager.h" 的 .cpp 立即拿到宏
// 和宏展开所需的全部类型，不必额外 include log.h。
#define LOG_DEBUG(msg) LoggerManager::instance().log("work", LogLevel::DEBUG, msg)
#define LOG_INFO(msg) LoggerManager::instance().log("work", LogLevel::INFO, msg)
#define LOG_WARNING(msg) LoggerManager::instance().log("work", LogLevel::WARNING, msg)
#define LOG_ERROR(msg) LoggerManager::instance().log("work", LogLevel::ERROR, msg)
