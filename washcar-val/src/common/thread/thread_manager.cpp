#include "common/thread/thread_manager.h"

#include "common/log/log_manager.h"

#include <algorithm>
#include <exception>

namespace {
/// 线程函数返回/异常退出时自动销号。放成 RAII，保证任何返回路径都会减。
class LiveGuard {
public:
    explicit LiveGuard(const std::string& name) : name_(name) {}
    ~LiveGuard() { ThreadManager::instance().release(name_); }
    LiveGuard(const LiveGuard&) = delete;
    LiveGuard& operator=(const LiveGuard&) = delete;

private:
    std::string name_;
};
}  // namespace

ThreadManager& ThreadManager::instance() {
    static ThreadManager m;
    return m;
}

void ThreadManager::setLimit(int n) {
    std::lock_guard<std::mutex> lock(m_);
    limit_ = (n > 0) ? static_cast<size_t>(n) : 16;
}

int ThreadManager::limit() const {
    std::lock_guard<std::mutex> lock(m_);
    return static_cast<int>(limit_);
}

int ThreadManager::count() const {
    std::lock_guard<std::mutex> lock(m_);
    return static_cast<int>(live_.size());
}

int ThreadManager::rejectedCount() const {
    std::lock_guard<std::mutex> lock(m_);
    return static_cast<int>(rejected_);
}

bool ThreadManager::acquire(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_);
    if (name.empty()) {
        return false;
    }
    if (live_.size() >= limit_) {
        ++rejected_;
        return false;
    }
    live_.push_back(name);
    return true;
}

void ThreadManager::release(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_);
    const auto it = std::find(live_.begin(), live_.end(), name);
    if (it != live_.end()) {
        live_.erase(it);
    }
}

std::string ThreadManager::snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    if (live_.empty()) {
        return "(none)";
    }
    // 同名线程理论上不该出现（名字即身份），出现多个就带上序号
    std::string out;
    for (size_t i = 0; i < live_.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += live_[i];
    }
    return out;
}

// -----------------------------------------------------------------------------

ManagedThread::~ManagedThread() {
    join();
}

ManagedThread::ManagedThread(ManagedThread&& other) noexcept
    : name_(std::move(other.name_)), thread_(std::move(other.thread_)) {
    other.name_.clear();
}

ManagedThread& ManagedThread::operator=(ManagedThread&& other) noexcept {
    if (this != &other) {
        join();
        name_ = std::move(other.name_);
        thread_ = std::move(other.thread_);
        other.name_.clear();
    }
    return *this;
}

bool ManagedThread::start(const std::string& name, std::function<void()> fn) {
    if (!fn) {
        return false;
    }
    if (thread_.joinable()) {
        LOG_WARNING("[Thread] " + name + " 已启动，忽略重复 start()");
        return false;
    }
    if (!ThreadManager::instance().acquire(name)) {
        LOG_ERROR("[Thread] 线程数已达上限 " + std::to_string(ThreadManager::instance().limit())
                  + "，拒绝启动: " + name
                  + "（现有: " + ThreadManager::instance().snapshot() + "）");
        return false;
    }

    name_ = name;
    thread_ = std::thread([name, fn]() {
        LiveGuard guard(name);
        try {
            fn();
        } catch (const std::exception& e) {
            // 任一循环里抛异常只结束这一个线程，绝不让整个进程 abort
            LoggerManager::instance().log("work", LogLevel::ERROR,
                                          "[Thread] " + name + " 异常退出: " + e.what());
        } catch (...) {
            LoggerManager::instance().log("work", LogLevel::ERROR,
                                          "[Thread] " + name + " 异常退出: 未知异常");
        }
    });
    return true;
}

void ManagedThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
    name_.clear();
}

bool ManagedThread::joinable() const {
    return thread_.joinable();
}

void ManagedThread::detach_state() {
    name_.clear();
}
