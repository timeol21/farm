#include "business_layer/timer.h"

#include "common/log/log_manager.h"

#include <algorithm>
#include <chrono>
#include <exception>

struct Timer::Task {
    std::string name;
    std::atomic<int> intervalMs{0};
    TimerTaskMode mode{TimerTaskMode::INLINE};
    std::function<void()> fn;
    std::int64_t lastRunMs = 0;      // 0 = 还没跑过, 首轮立即执行
    std::atomic<bool> stopRequested{false};
    std::thread worker;
};

std::int64_t Timer::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

Timer::~Timer() {
    stop();
}

void Timer::addTask(const std::string& name, int intervalMs, TimerTaskMode mode,
                    std::function<void()> task) {
    if (name.empty() || !task) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& existing : m_tasks) {
        if (existing->name == name) {
            existing->intervalMs.store(std::max(1, intervalMs));
            existing->mode = mode;
            existing->fn = std::move(task);
            return;
        }
    }

    auto item = std::make_shared<Task>();
    item->name = name;
    item->intervalMs.store(std::max(1, intervalMs));
    item->mode = mode;
    item->fn = std::move(task);

    if (m_running.load() && mode == TimerTaskMode::WORKER) {
        item->worker = std::thread(&Timer::workerLoop, this, item);
    }
    m_tasks.push_back(std::move(item));
    LOG_INFO("[Timer] addTask name=" + name + ", intervalMs=" + std::to_string(intervalMs)
             + ", mode=" + (mode == TimerTaskMode::WORKER ? "worker" : "inline"));
}

void Timer::removeTask(const std::string& name) {
    std::shared_ptr<Task> target;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            if ((*it)->name != name) continue;
            target = *it;
            m_tasks.erase(it);
            break;
        }
        if (!target) return;
        target->stopRequested.store(true);

        // 任务在自己的线程里调 removeTask: 不能 join 自己, 先寄存
        if (target->worker.joinable() && target->worker.get_id() == std::this_thread::get_id()) {
            m_retired.push_back(target);
            LOG_INFO("[Timer] removeTask(deferred) name=" + name);
            return;
        }
    }

    if (target->worker.joinable()) {
        target->worker.join();
    }
    LOG_INFO("[Timer] removeTask name=" + name);
}

void Timer::setInterval(const std::string& name, int intervalMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_tasks) {
        if (item->name == name) {
            item->intervalMs.store(std::max(1, intervalMs));
            return;
        }
    }
}

void Timer::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;

    std::vector<std::shared_ptr<Task>> workers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_tasks) {
            if (item->mode == TimerTaskMode::WORKER && !item->worker.joinable()) {
                workers.push_back(item);
            }
        }
    }
    for (auto& item : workers) {
        item->worker = std::thread(&Timer::workerLoop, this, item);
    }

    m_scheduler = std::thread(&Timer::schedulerLoop, this);
    LOG_INFO("[Timer] start, tasks=" + std::to_string(taskCount()));
}

void Timer::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    std::vector<std::shared_ptr<Task>> all;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        all = m_tasks;
        all.insert(all.end(), m_retired.begin(), m_retired.end());
        for (auto& item : all) item->stopRequested.store(true);
        m_tasks.clear();
        m_retired.clear();
    }

    if (m_scheduler.joinable() && m_scheduler.get_id() != std::this_thread::get_id()) {
        m_scheduler.join();
    }
    for (auto& item : all) {
        // 在任务自己的线程里被 stop 时不能 join 自己
        if (item->worker.joinable() && item->worker.get_id() != std::this_thread::get_id()) {
            item->worker.join();
        }
    }
    LOG_INFO("[Timer] stop, stoppedTasks=" + std::to_string(all.size()));
}

bool Timer::isRunning() const {
    return m_running.load();
}

std::size_t Timer::taskCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

void Timer::schedulerLoop() {
    while (m_running.load()) {
        std::vector<std::shared_ptr<Task>> due;
        const std::int64_t now = nowMs();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& item : m_tasks) {
                if (item->mode != TimerTaskMode::INLINE || item->stopRequested.load()) continue;
                const int interval = std::max(1, item->intervalMs.load());
                if (item->lastRunMs == 0 || now - item->lastRunMs >= interval) {
                    item->lastRunMs = now;
                    due.push_back(item);
                }
            }
        }
        for (auto& item : due) {
            try {
                item->fn();
            } catch (const std::exception& e) {
                LOG_WARNING(std::string("[Timer] task exception name=") + item->name + ", err=" + e.what());
            } catch (...) {
                LOG_WARNING(std::string("[Timer] task exception name=") + item->name);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void Timer::workerLoop(const std::shared_ptr<Task>& task) {
    while (m_running.load() && !task->stopRequested.load()) {
        const int interval = std::max(1, task->intervalMs.load());
        const std::int64_t now = nowMs();
        if (task->lastRunMs == 0 || now - task->lastRunMs >= interval) {
            task->lastRunMs = now;
            try {
                task->fn();
            } catch (const std::exception& e) {
                LOG_WARNING(std::string("[Timer] task exception name=") + task->name + ", err=" + e.what());
            } catch (...) {
                LOG_WARNING(std::string("[Timer] task exception name=") + task->name);
            }
            continue;
        }
        // 分片睡眠: 改周期/停任务都能在一个分片内响应
        std::int64_t sleepMs = task->lastRunMs + interval - nowMs();
        if (sleepMs > 50) sleepMs = 50;
        if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}
