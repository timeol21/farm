#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "common/log/log_object.h"

class ILogQueue{
public:
    virtual ~ILogQueue() = default;

    virtual void push(const LogMessage& meg) = 0;

    virtual bool pop(LogMessage& meg) = 0;

    virtual void notifyAll() = 0; // 唤醒所有等待的线程

};


class BlockingQueue : public ILogQueue {
public:
    void push(const LogMessage& msg) override;

    bool pop(LogMessage& msg) override;


    void notifyAll() override;

private:
    std::queue<LogMessage> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv; // 线程的 “闹钟 + 等待室”
    bool m_stop{false};
};