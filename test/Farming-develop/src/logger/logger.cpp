#include "logger/logger.h"
#include <iostream>
#include "config/config.h"
#include "utils/time_utils.h"
#include "utils/file_utils.h"

Logger::Logger() : logPath("../log/dingchang3588.log") {
	initLogFile();
}


Logger::~Logger() {
	if (logFile.is_open()) {
		logFile.close();
	}
}

void Logger::initLogFile() {
	if (logFile.is_open()) {
		logFile.close();
	}

	size_t pos = logPath.find_last_of('/');
	if (pos != std::string::npos) {
		std::string dirPath = logPath.substr(0, pos);
		createDirectory(dirPath);
	}
	logFile.open(logPath, std::ios::out | std::ios::app);
	if (!logFile.is_open()) {
		std::cerr << "Failed to open log file: " << logPath << std::endl;
	}
}

Logger& Logger::getInstance() {
	static Logger instance;
	return instance;
}

void Logger::setLogPath(const std::string& log_path) {
	if (logFile.is_open()) {
		logFile.close();
	}
	logPath = log_path;

	size_t lastSlashPos = logPath.find_last_of('/');
	if (lastSlashPos != std::string::npos) {
		std::string dirPath = logPath.substr(0, lastSlashPos);
		createDirectory(dirPath);
	}

	logFile.open(logPath, std::ios::out | std::ios::app);
	if (!logFile.is_open()) {
		std::cerr << "Failed to open log file: " << logPath << std::endl;
	}
}

void Logger::log(LogLevel level, const std::string& message) {
	std::string timeStr = getCurrentTime();
	std::string levelStr = logLevelToString(level);
	std::string logMsg = "[" + timeStr + "][" + levelStr + "] " + message + "\n";

	std::cout << logMsg;
	if (logFile.is_open()) {
		logFile << logMsg;
		logFile.flush();
	}
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