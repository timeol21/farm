#pragma once
#include "log_level.h"
#include "logger.h"
#include <string>
#include <mutex>
#include <fstream>   
class LogManager
{
public:
    static LogManager& instance();
    void init(const std::string& logPath);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

private:
    LogManager() = default;
    ~LogManager()
    {
        if(logFile_.is_open())
        {
            logFile_.close();
        }
    }
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

private:
    void write(LogLevel level,const std::string& message);

private:
    std::ofstream logFile_; 
    std::string logPath_;
    bool initialized_ = false;
    std::mutex mutex_;
};



