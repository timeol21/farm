#pragma once

#include "common/thread/thread_manager.h"

// 业务层的上报调度器。
//
// 只做一件事：按 timing 配置的周期调洗车服务的三路上报任务。
// 不碰 PLC、不碰 MQTT、不碰队列 —— 报文由 WashCarService 通过 IReportSink 推出去，
// 推到哪里（内存队列 / 直接发 / 落盘）是通讯层的事。
//
// 断链时不需要在这里特殊处理：上行队列对 monitor / readdata 是"只留最新"，
// 堆积时会自我收敛，重连后发出的就是最新状态。

#include "business_layer/washcar/washcar_service.h"

#include <atomic>
#include <string>
#include <thread>

class ReportScheduler {
public:
    explicit ReportScheduler(IWashCarService& service);
    ~ReportScheduler();

    ReportScheduler(const ReportScheduler&) = delete;
    ReportScheduler& operator=(const ReportScheduler&) = delete;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    void loop();

    IWashCarService& service_;

    ManagedThread thread_;
    std::atomic<bool> running_{false};

    // 启动时从 ConfigManager 固化一份，避免每轮都读配置
    int valueChangeIntervalMs_{3000};
    int faultReportIntervalMs_{3000};
    int dailyCheckIntervalMs_{60000};
    int dailyReportHour_{8};
    int dailyReportMinute_{0};

    // 当天是否已经触发过日报（"YYYY-MM-DD"）
    std::string lastDailyReportDate_;
};
