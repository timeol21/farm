#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H
#include "video_service.h"
#include "video_data_object.h"
// #include "isensor_manager.h"
#include "idevice_manager.h"
#include "sensor_service.h"
#include "controller_service.h"
#include "device_info.h"
class DeviceManager : public IDeviceManager{
public:
    DeviceManager();
    ~DeviceManager();
    DeviceStatus getStatus() override;
    VideoDerviceStatusInfo  getCameraStatus() override;

    VideoFrames getAllRealImage() override;
    PreviewFrame getRealImage(const std::string& camId , const std::string& nvrId) override;
    void getAllHistoryImage() override;

    void getHistoryImage(const std::string& camId) override;
    void operateCamera() override;
    void operatePlc(const std::string &deviceId, const std::string &cmd) override;
    void updateConfig() override;
    void queryRecordFiles(std::string camId_,std::string startTime_,std::string endTime_,VideoFiles videoFiles) override;

    bool downloadRecordFile(DownloadVideoFile& in, DownloadReadyFile& out) override;
        // =================== 门锁控制（由控制器服务管理，仅开锁）===================
    DoorLockOperationResult openDoorLock(const std::string& lockId) override;
    
    // 读取所有温湿度传感器数据
    std::vector<SensorStatusData> getAllTemperatureHumidity() override;
    
    // 设置告警回调（监听传感器状态：红外、水浸、烟感触发时回调）- 用于日志
    void setAlarmCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    // 设置报警任务回调（当传感器异常时自动提交报警任务到调度器）
    void setAlarmTaskCallback(AlarmTaskCallback callback);
    
    // 获取所有传感器状态（供内部定时上报使用）
    AllSensorStatus getAllSensorStatus();
private:
    std::shared_ptr<IVideoService> videoService_;
    std::shared_ptr<ISensorService> sensorService_;
    std::shared_ptr<IControllerService> controllerService_;
    // std::shared_ptr<IPLCManager> plcManager_;
    // std::shared_ptr<ISensorManager> sensorManager_;

};

#endif