#ifndef DEVICE_STATUS_REPORTER_H
#define DEVICE_STATUS_REPORTER_H
//设备状态定时上传
#include "idevice_manager.h"
#include "itask_result_publisher.h"
#include <thread>
#include <atomic>
#include <string>

class DeviceStatusReporter {
public:
    DeviceStatusReporter(IDeviceManager* devMgr,
                         ITaskResultPublisher* publisher);

    ~DeviceStatusReporter();

    void reportStatus(const std::string& topic);

    void startAutoReport(const std::string& topic, int intervalSec);
    void stopAutoReport();

private:
    void autoReportLoop();

private:
    IDeviceManager* devMgr_;
    ITaskResultPublisher* publisher_;

    std::string topic_;
    int intervalSec_ = 5;
    std::atomic_bool running_ = false;
    std::thread worker_;
};

#endif