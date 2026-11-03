#pragma once
#include <string>
#include <ctime>
#include <thread>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,//Exception
    ERROR
};

struct LogMessage {
    LogLevel level;
    std::string message;

    std::time_t timestamp;
    std::thread::id threadId;

    std::string module;   // 预留：模块名（方案三用）
};