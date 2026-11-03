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
    explicit FileSink(const std::string& dir);

    void write(const LogMessage& msg) override;

private:
    std::ofstream m_file;
    std::string m_filePath;
    std::mutex mtx;
    std::string m_basePath;   // 日志目录，如 <storageRoot>/log
    std::string m_currentDate;

    // 保留天数。现场 PLC 断线时日志一天能到 80MB，必须有上限。
    static constexpr int kKeepDays = 7;

    // 预留：日志滚动
private:
    std::string buildFileName(const std::string& date);

    void rotateIfNeeded();
    void cleanupOldLogs() const;

};

// 把日志原样写到 stdout —— 现场调试时终端能直接看到
class ConsoleSink : public ILogSink {
public:
    void write(const LogMessage& msg) override;
};