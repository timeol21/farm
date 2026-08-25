#ifndef I_DEVICE_MANAGER_H
#define I_DEVICE_MANAGER_H
#include <string>
#include <sensor_service.h>
#include <device_info.h>
#include "video_data_object.h"

class IDeviceManager{
public:
    virtual ~IDeviceManager() = default;
    virtual DeviceStatus getStatus() = 0;
    virtual VideoDerviceStatusInfo getCameraStatus() = 0; //设备状态获取

    virtual PreviewFrame getRealImage(const std::string& camId , const std::string& nvrId) = 0; //获取对应摄像头实时图片
    virtual VideoFrames getAllRealImage() = 0; //获取所有摄像头实时图片
    virtual void getHistoryImage(const std::string& camId) = 0;
    virtual void getAllHistoryImage() = 0;
    
    virtual void operateCamera() = 0;
    virtual void operatePlc(const std::string &deviceId, const std::string &cmd) = 0;

    virtual void updateConfig() = 0; //更新配置

    virtual void queryRecordFiles(std::string camId_,std::string startTime_,std::string endTime_,VideoFiles videoFiles) = 0;


    virtual bool downloadRecordFile(DownloadVideoFile& in, DownloadReadyFile& out) = 0;

    // =================== 传感器相关接口 ===================
    
    // 读取所有温湿度传感器数据
    virtual std::vector<SensorStatusData> getAllTemperatureHumidity() = 0;
    
    // 门锁控制（仅开锁）
    virtual DoorLockOperationResult openDoorLock(const std::string& lockId) = 0;
};



#endif