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
    /// dir    : 日志目录，如 <storageRoot>/log
    /// keepDays: 保留天数，来自 config.json 的 box.logKeepDays（默认 7）。
    ///           现场 PLC 断线时日志一天能到 80MB，必须有上限，否则把盒子磁盘写满。
    /// prefix : 文件名前缀。业务日志 "log_"，系统状态监控专用 "sys_"。
    ///          前缀同时决定 cleanupOldLogs 的删除范围——两个 sink 互不干扰。
    /// plain  : true = 只写 "[时间] 消息"，不写 [级别]/[T:线程号]/[模块]。
    ///          周期性指标文件用这个，一行一条，便于 grep 和画曲线。
    explicit FileSink(const std::string& dir, int keepDays = 7,
                      const std::string& prefix = "log_", bool plain = false);

    void write(const LogMessage& msg) override;

private:
    std::ofstream m_file;
    std::string m_filePath;
    std::mutex mtx;
    std::string m_basePath;   // 日志目录，如 <storageRoot>/log
    std::string m_currentDate;
    std::string m_prefix{"log_"};
    bool m_plain{false};

    int m_keepDays{7};

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