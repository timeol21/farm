#include "common/log/logger.h"
#include "common/log/log_manager.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance;
}

void Logger::debug(const std::string& message)
{
    LogManager::instance().debug(message);
}


void Logger::info(const std::string& message)
{
    LogManager::instance().info(message);
}


void Logger::warning(const std::string& message)
{
    LogManager::instance().warning(message);
}


void Logger::error(const std::string& message)
{
    LogManager::instance().error(message);
}


void Logger::fatal(const std::string& message)
{
    LogManager::instance().fatal(message);
}

void LogManager::init(const std::string& logPath)
{
    std::lock_guard<std::mutex> lock(mutex_);

    logPath_ = logPath;

    logFile_.open(logPath_, std::ios::app);

    initialized_ = logFile_.is_open();
}

void LogManager::debug(const std::string& message)
{
    write(LogLevel::DEBUG, message);
}


void LogManager::info(const std::string& message)
{
    write(LogLevel::INFO, message);
}


void LogManager::warning(const std::string& message)
{
    write(LogLevel::WARNING, message);
}


void LogManager::error(const std::string& message)
{
    write(LogLevel::ERROR, message);
}


void LogManager::fatal(const std::string& message)
{
    write(LogLevel::FATAL, message);
}

void LogManager::write(LogLevel level,const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string levelString;
    switch(level) {
    case LogLevel::DEBUG:
        levelString = "DEBUG";
        break;
    case LogLevel::INFO:
        levelString = "INFO";
        break;
    case LogLevel::WARNING:
        levelString = "WARNING";
        break;
    case LogLevel::ERROR:
        levelString = "ERROR";
        break;
    case LogLevel::FATAL:
        levelString = "FATAL";
        break;
    }
    // win下这样 localtime_s改成localtime_r
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    // localtime_s(&localTime,&time);
    #ifdef _WIN32
        localtime_s(&localTime,&time);
    #else
        localtime_r(&time,&localTime);
    #endif

    std::stringstream ss;
    ss << std::put_time(&localTime,"%Y-%m-%d %H:%M:%S");

    std::string log ="[" +ss.str() +"] [" +levelString +"] " +message;
    
    std::cout<< log << std::endl;
    
    if(initialized_ && logFile_.is_open())
    {
        logFile_ << log << std::endl;
    }
}