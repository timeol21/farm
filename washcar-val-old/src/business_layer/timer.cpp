#include "business_layer/timer.h"
#include "common/log/log_manager.h"  // LOG_* 宏现已在 log_manager.h 中定义
#include <mutex>

Timer::~Timer() {
    // 兜底 join：保证 uploadthread 析构前已 join，否则 std::thread 析构会 std::terminate
    stopUpload();
}

void Timer::TimingProcessing(){
    auto last_time = std::chrono::steady_clock::now();//获取当前时间（系统里面的时间）
    while(m_running) {
        //获取的逻辑
        // std::cout << "更新帧缓冲（周期：" << g_device_interval << "ms）" << std::endl;

        last_time += std::chrono::milliseconds(g_device_interval);
        std::this_thread::sleep_until(last_time);//执行的是逻辑时间 + 休息时间 = 设置的时间 例如 逻辑花 2ms + sleep 28ms = g_device_interval
    }
};


void Timer::scheduleRepeated(int intervalMs, std::function<void()> task){
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_tasks.push_back({intervalMs, 0, task});
    }
    LOG_INFO(std::string("[Timer] scheduleRepeated intervalMs=") + std::to_string(intervalMs) + ", totalTasks=" + std::to_string(m_tasks.size()));

    bool expected = false;
    if(!m_runningUpload.compare_exchange_strong(expected,true)) {
        return ; //已经运行了
    }

    // 防御：旧线程若还在 joinable（异常路径下），先收尸
    if (uploadthread.joinable()) {
        uploadthread.join();
    }
    uploadthread = std::thread(&Timer::runLoop, this);
    LOG_INFO("[Timer] runLoop thread started");
}

void Timer::runLoop() {
    while (m_runningUpload.load()) {
        auto now = std::chrono::steady_clock::now();
        auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

        std::vector<std::function<void()>> tasksToRun;
        {
            std::lock_guard<std::mutex> lock(m_taskMutex);
            for (auto& t : m_tasks) {
                if (t.lastExecuteTime == 0) {
                    t.lastExecuteTime = nowMs;
                    tasksToRun.push_back(t.task);
                } else if (nowMs - t.lastExecuteTime >= t.intervalMs) {
                    t.lastExecuteTime = nowMs;
                    tasksToRun.push_back(t.task);
                }
            }
        }

        for (auto& task : tasksToRun) {
            try {
                if (task) {
                    task();
                }
            } catch (...) {
                LOG_WARNING("[Timer] task exception");
            }
        }
        //LOG_DEBUG(std::string("[Timer] runLoop tick, fired=") + std::to_string(tasksToRun.size()));

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    LOG_INFO("[Timer] runLoop thread exiting");
}

void Timer::stopUpload(){
    LOG_INFO("[Timer] stopUpload");
    m_runningUpload.store(false);
    if (uploadthread.joinable()) {
        uploadthread.join();
    }
}

bool Timer::isRunningUpload() const{
     return m_runningUpload.load();
}

void Timer::TimingPullVideoFrame(){
}