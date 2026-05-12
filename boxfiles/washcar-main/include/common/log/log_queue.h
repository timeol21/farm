#pragma once
#include "common/log/log_object.h"
#include <queue>
#include <mutex>
#include <condition_variable>
class ILogQueue {
public:
    virtual ~ILogQueue() = default;

    virtual void push(const LogMessage& msg) = 0;

    virtual bool pop(LogMessage& msg) = 0;//// 返回 false 表示被唤醒但无数据（例如 stop）

    virtual void notifyAll() = 0;
};



class BlockingQueue : public ILogQueue {
public:
    void push(const LogMessage& msg) override;

    bool pop(LogMessage& msg) override;


    void notifyAll() override;

private:
    std::queue<LogMessage> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop{false};
};