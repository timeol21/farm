#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <ctime>

enum class LogLevel{
    INFO,
    WARNING,
    ERROR,
};

class Logger{
public:    
    //获取单例实例
    static Logger& getInstance();
    //禁止拷贝构造和赋值操作
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    //设置日志文件路径
    void setLogPath(const std::string& log_path);
    //写入日志
    void log(LogLevel level, const std::string& message);

private:
    Logger();
    ~Logger();

    std::string getCurrentTime() const;

    std::string logLevelToString(LogLevel level) const;

    std::ofstream logFile;
    std::string logPath;
};

// 日志宏定义（简化日志调用）
#define LOG_INFO(msg) Logger::getInstance().log(LogLevel::INFO, msg)
#define LOG_WARNING(msg) Logger::getInstance().log(LogLevel::WARNING, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, msg)

#endif