#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger& getInstance();
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    
    void setLogLevel(LogLevel level);
    
private:
    Logger();
    ~Logger();
    
    void log(LogLevel level, const std::string& message);
    
    std::ofstream log_stream_;
    std::mutex mutex_;
    LogLevel current_level_ = LogLevel::INFO;
};

#endif