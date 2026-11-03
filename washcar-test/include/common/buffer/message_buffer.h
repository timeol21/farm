#pragma once

// 线程安全的有界队列（header-only 模板）。
//
// 用途：层与层之间的传输通道。生产者 push 后立即返回，消费者 popWait 取。
// 满时的行为由 push 的返回值体现——调用方自己决定要不要记日志/告警。
//
// 为什么用条件变量而不是"纯定时轮询"：
//   调用方给的 timeoutMs 就是"定时器频率"——空队列时最多等这么久，
//   有消息时立刻醒来。既满足按频率取，又不会忙等空转。

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

template <typename T>
class MessageBuffer {
public:
    void init(std::size_t capacity = 256) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        capacity_ = capacity == 0 ? 1 : capacity;
        dropped_ = 0;
        shutdown_ = false;
    }

    /// 入队。满了丢队首（最旧的），返回 false 表示丢了一条。
    bool push(T item) {
        bool ok = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                return false;
            }
            while (queue_.size() >= capacity_) {
                queue_.pop_front();
                ++dropped_;
                ok = false;
            }
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
        return ok;
    }

    /// 阻塞等到有数据，或超时。返回 false = 超时/已关闭。
    bool popWait(T& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto ready = [this] { return shutdown_ || !queue_.empty(); };
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)) {
            return false;
        }
        if (queue_.empty()) {
            return false;   // shutdown 唤醒
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t droppedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    /// 停止：唤醒所有等待者，之后的 push 一律拒绝
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
            queue_.clear();
        }
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    std::size_t capacity_{256};
    std::size_t dropped_{0};
    bool shutdown_{false};
};
