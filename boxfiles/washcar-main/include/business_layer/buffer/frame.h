#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>

template<typename FrameType>
class FrameBuffer {
public:
    explicit FrameBuffer() {}

    // 生产者
    void push(std::shared_ptr<FrameType> frame) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 队列满了就丢弃最旧帧
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
        }

        queue_.push_back(std::move(frame));
        cond_.notify_one();
    }

    // 消费者（阻塞）
    std::shared_ptr<FrameType> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        cond_.wait(lock, [this] {
            return !queue_.empty();
        });

        auto frame = queue_.front();
        queue_.pop_front();
        return frame;
    }

    // 非阻塞获取
    std::shared_ptr<FrameType> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return nullptr;

        auto frame = queue_.front();
        queue_.pop_front();
        return frame;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<std::shared_ptr<FrameType>> queue_;
};