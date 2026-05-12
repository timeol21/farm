#pragma once
#include <string>
#include <fstream>
#include <ctime>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include "common/log/log_object.h"
#include <filesystem>
class ILogSink {
public:
    virtual ~ILogSink() = default;

    virtual void write(const LogMessage& msg) = 0;
};

class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& filePath);

    void write(const LogMessage& msg) override;

private:
    std::ofstream m_file;
    std::string m_filePath;
    std::mutex mtx;
    std::string m_basePath;   // "log"
    std::string m_currentDate;

    // 预留：日志滚动
private:
    std::string buildFileName(const std::string& date);

    void rotateIfNeeded();
    
};