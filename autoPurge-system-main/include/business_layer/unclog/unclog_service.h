#pragma once

#include "business_layer/command/command_service.h"
#include "business_layer/detection/detection_service.h"
#include "business_layer/device/device_service.h"
#include "business_layer/safety/safety_service.h"

#include "business_layer/unclog/unclog_service_object.h"
#include "business_layer/unclog/observer.h"

#include "data_layer/unclog/unclog_dao.h"
#include "data_layer/alarm/alarm_dao.h"

#include <atomic>
class IUnclogService{
public:
    virtual ~IUnclogService() = default;
     // ===== 外部控制 =====
    virtual bool requestStart() = 0;     // 请求启动清堵（手动）
    virtual bool requestStop() = 0;      // 请求停止

    virtual bool switchMode(SystemMode mode) = 0; // 巡检 / 清堵 / 运维

    // ===== 查询 =====
    virtual UnclogStatus getStatus() const = 0;
    virtual AlarmInfo getAlarm() const = 0;

    // 操作各种设备

    //查看当前帧

};




class UnclogService : public IUnclogService, public ILifecycle , public Observer{ 
public:
    UnclogService(IDeviceService& device,IDetectionService& detection,IUnclogDao& unclogDao,IAlarmDao& alarmDao);
    
    ~UnclogService() override; 

    void start() override;

    void stop() override;


    bool requestStart() override;     // 请求启动清堵（手动）

    bool requestStop() override;      // 请求停止

    bool switchMode(SystemMode mode) override; // 巡检 / 清堵 / 运维

    UnclogStatus getStatus() const override;

    AlarmInfo getAlarm() const override;

    void onBlockDetected(const DetectionResult& result) override;

private:
    void startUnclogFlow();     
    void stopUnclogFlow();     

    void executeUnclogStep();   

    // ===== 状态机 =====
    bool canSwitchToUnclog() const; 

    // ===== 监听 =====
    void monitorDuringUnclog(); //判断流程 (清堵)

    // ===== 异常处理 =====
    void handleError(const std::string& reason);
    
    void safeStopDevices();

    bool executeWithCheck(const DeviceCommand& cmd,const std::string& errMsg);

    


private:
    IDeviceService& m_deviceService; // 设备

    IDetectionService& m_detectionService; // ai检测


    IUnclogDao& m_unclogDao; // 清堵dao

    IAlarmDao& m_alarmDao; // 警告
    
    //清堵状态
    UnclogState m_state;

    //清堵设备
    UnclogDevices devices;

    //保证服务正常执行 不同步堵塞
    std::thread m_worker;
    std::atomic<bool> m_running{false};
};