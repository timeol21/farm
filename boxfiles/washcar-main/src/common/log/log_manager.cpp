#include "common/log/log_manager.h"
LoggerManager& LoggerManager::instance()
{
    static LoggerManager instance;
    return instance;
}

std::shared_ptr<ILogger> LoggerManager::getLogger(const std::string& name)
{
    std::shared_lock lock(m_mutex); // 读锁

    auto it = m_loggers.find(name);
    if (it != m_loggers.end()) {
        return it->second;
    }
    return nullptr;
}

void LoggerManager::registerLogger(const std::string& name,
                                   std::shared_ptr<ILogger> logger)
{
    std::unique_lock lock(m_mutex); // 写锁
    m_loggers[name] = logger;
}