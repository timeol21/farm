#include "common/log/log_queue.h"
#include <iostream>
void BlockingQueue::push(const LogMessage& msg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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