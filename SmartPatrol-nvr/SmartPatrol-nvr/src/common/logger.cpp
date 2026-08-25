#include "common/logger.h"
#include "common/config.h"
#include <iostream>
#include <chrono>
#include <iomanip>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    auto& config = Config::getInstance();
    std::string log_file = config.getString("system.log_file", "/var/log/rk3588_service.log");
    log_stream_.open(log_file, std::ios::app);
}

Logger::~Logger() {
    if (log_stream_.is_open()) {
        log_stream_.close();
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }
    
    std::cout << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
              << "[" << level_str << "] " << message << std::endl;
              
    if (log_stream_.is_open()) {
        log_stream_ << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
                   << "[" << level_str << "] " << message << std::endl;
        log_stream_.flush();
    }
}