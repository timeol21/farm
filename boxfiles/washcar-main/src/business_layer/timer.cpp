#include "business_layer/timer.h"
#include "common/log/log_manager.h"

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
    taskUpload = task;
    bool expected = false;
    if(!m_runningUpload.compare_exchange_strong(expected,true)) {
        return ; //已经运行了
    }

    m_intervalMs = intervalMs;
    uploadthread = std::thread([this](){
        while(m_runningUpload.load()){
            try{
                if(taskUpload){
                    taskUpload();
                }
            } catch(...){
                LOG_WARNING("[Timer] task exception");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
        }
    });
}

void Timer::stopUpload(){
m_runningUpload.store(false);

    if (uploadthread .joinable()) {
        uploadthread .join();
    }
}

bool Timer::isRunningUpload() const{
     return m_runningUpload.load();
}

void Timer::TimingUpload(){
    //这里去拿数据
};

void Timer::TimingPullVideoFrame(){
    auto last_time = std::chrono::steady_clock::now();
    while(m_running) {
        //获取的逻辑
        // std::cout << "更新帧缓冲（周期：" << g_frame_interval << "ms）" << std::endl;

        last_time += std::chrono::milliseconds(g_frame_interval);
        std::this_thread::sleep_until(last_time);
    }
};