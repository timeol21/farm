#pragma once

// 上行缓冲：专门给"发往云端"的报文用。
//
// 与通用 MessageBuffer 的区别是"同类只留最新"：
//   monitor / readdata 描述的是设备当前状态，堆十条历史状态没有意义，
//   新报文直接替换掉队列里同类旧报文，云端拿到的永远是最新快照。
//
//   pushalarm / commandreply 是事件：故障报警丢掉等于漏报，命令回执丢掉
//   云端会以为命令没执行。这两类逐条排队，队列满才丢最旧（并计数）。
//
// 断链期间：泵发不出去就不取，队列按上面规则自我收敛，不会无限膨胀。

#include "common/message.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>

class UplinkBuffer : public IReportSink {
public:
    void init(std::size_t capacity = 128) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        capacity_ = capacity == 0 ? 1 : capacity;
        dropped_ = 0;
        replaced_ = 0;
        shutdown_ = false;
    }

    /// 入队。可覆盖的类别会替换掉队列里同类旧报文。
    void push(Report report) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (shutdown_) {
                return;
            }

            if (report.replaceable()) {
                for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                    if (it->kind == report.kind) {
                        queue_.erase(it);
                        ++replaced_;
                        break;
                    }
                }
            }

            while (queue_.size() >= capacity_) {
                queue_.pop_front();
                ++dropped_;
            }

            queue_.push_back(std::move(report));
        }
        cv_.notify_one();
    }

    bool popWait(Report& out, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto ready = [this] { return shutdown_ || !queue_.empty(); };
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)) {
            return false;
        }
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

    /// 被同类新报文顶掉的数量（monitor 的正常现象，不算异常）
    std::size_t replacedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return replaced_;
    }

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
    std::deque<Report> queue_;
    std::size_t capacity_{128};
    std::size_t dropped_{0};
    std::size_t replaced_{0};
    bool shutdown_{false};
};
