#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "common/log/log.h"
#include <shared_mutex>
class LoggerManager {
public:
    static LoggerManager& instance();

    std::shared_ptr<ILogger> getLogger(const std::string& name);

    void registerLogger(const std::string& name,std::shared_ptr<ILogger> logger);


    
private:
    LoggerManager() = default;

private:
    std::unordered_map<std::string, std::shared_ptr<ILogger>> m_loggers;
    mutable std::shared_mutex m_mutex; // ⭐ 新增
};