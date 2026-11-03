#include "common/log/log_system.h"

#include "common/config/config_manager.h"
#include "common/log/log.h"
#include "common/log/log_manager.h"
#include "common/log/log_object.h"
#include "common/log/log_queue.h"
#include "common/log/log_sink.h"
#include "common/path/program_path.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

// LOG_* 宏固定往这个 logger 投递，名字只在这里出现一次。
constexpr const char* kLoggerName = "work";

// 日志目录相对程序目录的固定位置。config.json 里不再有这个字段。
constexpr const char* kLogDirInData = "data/log";

std::string s_logDir;

LogLevel parseLevel(const std::string& text) {
    if (text == "debug") {
        return LogLevel::DEBUG;
    }
    if (text == "warning" || text == "warn") {
        return LogLevel::WARNING;
    }
    if (text == "error") {
        return LogLevel::ERROR;
    }
    // info 以及任何认不出来的值都按 info：现场配置写错不该把日志吞掉
    return LogLevel::INFO;
}

}  // namespace

LogSystem& LogSystem::instance() {
    static LogSystem sys;
    return sys;
}

const std::string& LogSystem::dir() {
    return s_logDir;
}

bool LogSystem::init() {
    if (inited_) {
        return true;
    }

    const auto& cfg = ConfigManager::instance();

    // 目录由程序位置推导，不由配置指定：<程序目录>/data/log
    s_logDir = ProgramPath::join(kLogDirInData);
    if (s_logDir.empty()) {
        std::cerr << ">>> 错误: 无法确定日志目录（程序目录未初始化）" << std::endl;
        return false;
    }

    // 目录在这里显式建一次。FileSink 构造里也会建，但显式建的好处是
    // 权限不足能当场报出来，而不是等到第一条日志写不进去才发现。
    std::error_code ec;
    std::filesystem::create_directories(s_logDir, ec);
    if (ec) {
        std::cerr << ">>> 错误: 无法创建日志目录 " << s_logDir << ": " << ec.message() << std::endl;
        return false;
    }

    // 队列容量和日志保留天数都来自配置（queueCapacity.logCapacity / box.logKeepDays）。
    // 队列必须有上限：磁盘卡住时生产者还在 push，无界队列会把内存吃光。
    const std::size_t logCap = cfg.queueCapacity().logCapacity > 0
                                   ? static_cast<std::size_t>(cfg.queueCapacity().logCapacity)
                                   : 4096;
    auto queue  = std::make_shared<BlockingQueue>(logCap);
    auto logger = std::make_shared<AsyncLogger>(queue);

    logger->addSink(std::make_shared<FileSink>(s_logDir, cfg.box().logKeepDays));  // 落盘，现场取证靠它
    logger->addSink(std::make_shared<ConsoleSink>());      // 终端直看，调试方便
    logger->setLogLevel(parseLevel(cfg.logLevel()));

    logger_ = logger;

    // 先注册再启动：init 之后就能用 LOG_*，消息先攒在队列里，start 后统一刷出去。
    LoggerManager::instance().registerLogger(kLoggerName, logger_);

    inited_ = true;
    return true;
}

bool LogSystem::start() {
    if (!inited_) {
        std::cerr << ">>> 错误: 日志系统未初始化，不能启动" << std::endl;
        return false;
    }
    if (started_) {
        return true;
    }
    if (!logger_->start()) {
        std::cerr << ">>> 错误: 日志工作线程启动失败" << std::endl;
        return false;
    }
    started_ = true;

    // 启动信息。放在这里而不是 main，因为只有 start 之后这些内容才保证落盘。
    const auto& cfg = ConfigManager::instance();
    std::cout << ">>> 日志级别: " << cfg.logLevel()
              << ", 目录: " << s_logDir
              << ", 保留 " << cfg.box().logKeepDays << " 天"
              << ", 队列 " << cfg.queueCapacity().logCapacity << " 条" << std::endl;
    cfg.dumpToLog();
    return true;
}

void LogSystem::stop() {
    if (!started_) {
        return;
    }
    if (logger_) {
        logger_->stop();   // 内部会 join 线程并把队列剩余消息写完
    }
    started_ = false;
}
