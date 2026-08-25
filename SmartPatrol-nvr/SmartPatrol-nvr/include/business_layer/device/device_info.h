#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

// #include "camera_info.h"
// #include "plc_info.h"
// #include "sensor_types.h"
#include <vector>
#include <video_data_object.h>
#include <sensor_service.h>

// 设备状态汇总
struct DeviceStatus
{
    VideoDerviceStatusInfo cameraStatusList;
    AllSensorStatus sensorStatus;              // 传感器状态
};

struct RealImage
{
    FrameData frame;
    std::string sourceCameraId;
    std::string nvrId;
    bool integrity = false;
};

struct RealImageList
{
    std::vector<RealImage> RealImages;
    bool success = true;
};
// 传感器操作结果
struct SensorOperationResult {
    bool success = false;
    std::string message;
    SensorStatusData data;
};

// 门锁操作结果（仅开锁）
struct DoorLockOperationResult {
    bool success = false;
    std::string message;
};

#endif