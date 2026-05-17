#pragma once

#include <string>
#include <fstream>
#include "config/config.h"

class Logger {
private:
	Logger();
	~Logger();

	void initLogFile();
	std::string logLevelToString(LogLevel level) const;

	std::ofstream logFile;
	std::string logPath;
public:
	static Logger& getInstance();
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	void setLogPath(const std::string& log_path);

	void log(LogLevel level, const std::string& message);
};

#define LOG_INFO(msg) Logger::getInstance().log(LogLevel::INFO, msg)
#define LOG_WARNING(msg) Logger::getInstance().log(LogLevel::WARNING, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, msg)