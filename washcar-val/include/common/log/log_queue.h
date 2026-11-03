#pragma once
#include "common/log/log_object.h"
#include <cstddef>
#include <queue>
#include <mutex>
#include <condition_variable>

// 日志队列。
//
// **有界**：容量来自 config.json 的 queueCapacity.logCapacity（默认 4096）。
// 为什么要上限：日志生产者（采集线程/泵/业务）比消费者（落盘线程）快，
// 一旦磁盘卡住或日志暴增（PLC 断线时一天 80MB），无界队列会把内存吃光。
// 满了采取"丢最旧的"，并累加 dropped 计数 —— 宁可少几条历史日志，也不能 OOM。
class ILogQueue {
public:
    virtual ~ILogQueue() = default;

    virtual void push(const LogMessage& msg) = 0;

    virtual bool pop(LogMessage& msg) = 0;//// 返回 false 表示被唤醒但无数据（例如 stop）

    virtual void notifyAll() = 0;
};



class BlockingQueue : public ILogQueue {
public:
    explicit BlockingQueue(std::size_t capacity = 4096);

    void push(const LogMessage& msg) override;

    bool pop(LogMessage& msg) override;


    void notifyAll() override;

    /// 因队列满被丢弃的条数
    std::size_t droppedCount() const;

private:
    std::queue<LogMessage> m_queue;
    mutable std::mutex m_mutex;      // droppedCount() 是 const，需要 mutable
    std::condition_variable m_cv;
    bool m_stop{false};
    std::size_t m_capacity{4096};
    std::size_t m_dropped{0};
};
