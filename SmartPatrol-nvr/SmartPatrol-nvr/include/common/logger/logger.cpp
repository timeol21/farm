#include "logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>

Logger::Logger() : logFile() {
    // 默认日志路径：项目根目录下的log文件夹
    logPath = "./log/smartPatrol_nvr.log";
    // 创建log文件夹（若不存在）
    system(("mkdir -p " + logPath.substr(0, logPath.find_last_of('/'))).c_str());
    // 打开日志文件（追加模式）
    logFile.open(logPath, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logPath << std::endl;
    }
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

Logger& Logger::getInstance() {
    static Logger instance; // 静态局部变量，确保仅初始化一次
    return instance;
}

void Logger::setLogPath(const std::string& log_path) {
    if (logFile.is_open()) {
        logFile.close();
    }
    logPath = log_path;
    // 创建日志路径文件夹（若不存在）
    size_t lastSlashPos = logPath.find_last_of('/');
    if (lastSlashPos != std::string::npos) {
        std::string dirPath = logPath.substr(0, lastSlashPos);
        system(("mkdir -p " + dirPath).c_str());
    }
    // 重新打开日志文件
    logFile.open(logPath, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logPath << std::endl;
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::string timeStr = getCurrentTime();
    std::string levelStr = logLevelToString(level);
    std::string logMsg = "[" + timeStr + "] [" + levelStr + "] " + message + "\n";

    // 输出到控制台
    std::cout << logMsg;
    // 写入日志文件
    if (logFile.is_open()) {
        logFile << logMsg;
        logFile.flush(); // 强制刷新缓冲区，确保日志实时写入
    }
}

std::string Logger::getCurrentTime() const {
    std::time_t now = std::time(nullptr);
    std::tm* tmNow = std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(tmNow, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::logLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";  
        default:
            return "UNKNOWN";
    }
}