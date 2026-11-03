#include "common/log/log_queue.h"
#include <iostream>

BlockingQueue::BlockingQueue(std::size_t capacity)
    : m_capacity(capacity == 0 ? 1 : capacity) {}

void BlockingQueue::push(const LogMessage& msg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 满了丢最旧：日志是"回溯用"的，留最近的部分比留最早的有用。
        while (m_queue.size() >= m_capacity) {
            m_queue.pop();
            ++m_dropped;
        }
        m_queue.push(msg);
    }
    m_cv.notify_one(); // 唤醒一个等待线程
}

bool BlockingQueue::pop(LogMessage& msg)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // 等待直到队列非空
    m_cv.wait(lock, [this] {
        return !m_queue.empty() || m_stop;
    });
    
    if (m_queue.empty()) {
            return false;
    }
    msg = std::move(m_queue.front());
    m_queue.pop();
    return true;
}

void BlockingQueue::notifyAll()
{   
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all(); // 唤醒所有线程退出
}

std::size_t BlockingQueue::droppedCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dropped;
}
