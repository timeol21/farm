#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>

#include "common/log/log.h"

class LoggerManager{
public:

    static LoggerManager& instance();

    void registerLogger(const std::string& name, std::shared_ptr<ILogger> logger);

    std::shared_ptr<ILogger> getLogger(const std::string& name);
private:
    LoggerManager()  = default;
    ~LoggerManager()  = default;

    std::unordered_map<std::string, std::shared_ptr<ILogger>> loggers_;
    std::mutex mutex_;
};