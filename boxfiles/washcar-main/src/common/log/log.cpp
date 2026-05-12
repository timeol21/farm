#include "common/log/log.h"
#include <iostream>
#include <iomanip>
#include <sstream>

AsyncLogger::AsyncLogger(std::shared_ptr<ILogQueue> queue)
    : m_queue(queue)
{
}

AsyncLogger::~AsyncLogger() {
    stop();
}

bool AsyncLogger::start() {
    // 已经在运行，直接返回 false
    if(m_running) {
        std::cerr << "MQTT Service already running\n";
        return false;
    }

    // 标记启动中
    m_running = true;

    m_workerThread = std::thread(&AsyncLogger::worker, this);
    return true;  // 关键：成功返回 true
}

void AsyncLogger::stop() {
    if (!m_running) return;

    m_running = false;
    // 通知队列退出（如果你队列支持的话）
    if (m_queue) {
        m_queue->notifyAll();  // 需要你在 BlockingQueue 里实现
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void AsyncLogger::setLogLevel(LogLevel level) {
    m_level = level;
}

void AsyncLogger::addSink(std::shared_ptr<ILogSink> sink) {
    m_sinks.push_back(sink);
}

void AsyncLogger::log(LogLevel level, const std::string& message) {
    if (level < m_level) return;
    LogMessage msg;
    msg.level = level;
    msg.message = message;
    msg.timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    msg.threadId = std::this_thread::get_id();

    m_queue->push(msg);
}
void AsyncLogger::worker() {
    while (m_running) {
        LogMessage msg;

        // 阻塞等待
        if (!m_queue->pop(msg)) {
            continue; // 队列被唤醒但没数据

        }

        // 分发给所有 sink
        for (auto& sink : m_sinks) {
            sink->write(msg);
        }
    }

    // ⭐ 退出前 flush 剩余日志（非常关键）
    LogMessage msg;
    while (m_queue->pop(msg)) {
        for (auto& sink : m_sinks) {
            sink->write(msg);
        }
    }
}