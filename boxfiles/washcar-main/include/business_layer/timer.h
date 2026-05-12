#pragma once
#include "business_layer/buffer/equipment_status.h"
#include "business_layer/buffer/frame.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>

class ITimer {
public:
    virtual void TimingProcessing() = 0; //定时更新函数

    virtual void TimingPullVideoFrame() = 0; //定时拉取视频帧函数

    virtual void scheduleRepeated(int intervalMs, std::function<void()> task) = 0;

   
    virtual void stopUpload() = 0;

    // 是否正在运行
    virtual bool isRunningUpload() const = 0;


    virtual ~ITimer() = default;    
};

class Timer : public ITimer {
public:
    Timer() = default;
    ~Timer() = default;
    void TimingProcessing() override; //定时更新函数

    void TimingPullVideoFrame() override; //定时拉取视频帧函数

    void scheduleRepeated(int intervalMs, std::function<void()> task) override;

    void stopUpload() override;

    bool isRunningUpload() const override;
private:

    void TimingUpload() ; //定时上传函数

private:
    // 定时器相关成员变量，如定时器ID、时间间隔等
    std::atomic<bool> m_running{ false }; // 定时器运行状态（定时更新和拉帧？这个状态不会被开启）
    //std::thread m_frame; // 帧定时器线程
    //std::thread m_device_status; // 设备状态定时器线程
    std::atomic<int> g_frame_interval {30};  // 帧周期（ms），可动态修改
    std::atomic<int> g_device_interval {1000};// 设备周期（ms）

    std::atomic<int>  m_intervalMs{1000};
    std::atomic<bool> m_runningUpload{ false };
    std::function<void()> taskUpload;
    std::thread uploadthread;
};
