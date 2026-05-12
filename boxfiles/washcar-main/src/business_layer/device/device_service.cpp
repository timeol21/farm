#include "business_layer/device/device_service.h"
#include<optional>
#include "data_layer/acquisition_task/fx_device_acquisition_task.h" 

#include<iostream>




DeviceService::DeviceService(DeviceManageService& deviceManageService,
                             DeviceStatusCache& deviceStatusCache,
                             DeviceAcquisitionTask& deviceAcuqisitionTask,
                             RealTimeFrameCache& realTimeFrameCache) 
    : deviceManageService_(deviceManageService),
      deviceStatusCache_(deviceStatusCache),
      deviceAcquisitionTask_(deviceAcuqisitionTask),
      realTimeFrameCache_(realTimeFrameCache)
{
    startTimer();
}

DeviceService::~DeviceService() {
    stopTimer();
}

BoxDeviceStatus DeviceService::viewAllDeviceStatus() {
    if (deviceStatusCache_.isBoxDeviceStatusEmpty()){
        const BoxDeviceStatus& devicesStatus = deviceManageService_.getDeviceStatus();
        deviceStatusCache_.updateBoxDeviceStatus(devicesStatus);
        return devicesStatus;
    }
    
    const BoxDeviceStatus& devicesStatus = deviceStatusCache_.getBoxDeviceStatus();
    return devicesStatus;
}


// bool DeviceService::openFxSolenoid(const std::string& plcId, int yOctal) {
//     return deviceManageService_.operateFxSolenoid(plcId, yOctal, true);
// }

// bool DeviceService::closeFxSolenoid(const std::string& plcId, int yOctal) {
//     return deviceManageService_.operateFxSolenoid(plcId, yOctal, false);
// }
//置位调用 复位用100 停止用0 启动用241
bool DeviceService::forceFxM(const std::string& plcId, int mDecimal, bool turnOn) {
    return deviceManageService_.forceM(plcId, mDecimal, turnOn);
}
// bool DeviceService::readFxRegister(const std::string& plcId, int dNumber, uint16_t& value) {
//     return deviceManageService_.readFxRegister(plcId, dNumber, value);
// }

/*
DeviceOperationResult DeviceService::openSolenoidValue(const PlcDeviceInfo& info) {
    if(deviceStatusCache_.isSolenoidOpen(info))
        return DeviceOperationResult(-1,"电磁阀已经打开!");
    deviceManageService_.openSolenoidValue(info);
    return DeviceOperationResult(0,"电磁阀打开成功!");
}

DeviceOperationResult DeviceService::closeSolenoidValue(const PlcDeviceInfo& info) {
    if(deviceStatusCache_.isSolenoidClose(info))
        return DeviceOperationResult(-1,"电磁阀已经关闭!");

    return deviceManageService_.closeSolenoidValue(info);
}
*/
DeviceOperationResult DeviceService::lockDoorLock(const GPIODeviceSimpleInfo& info) {
    if(deviceStatusCache_.isDoorLockLock(info))
        return DeviceOperationResult(-1,"门锁已被锁上");
    return deviceManageService_.lockDoorLock(info);
}

DeviceOperationResult DeviceService::unlockDoorLock(const GPIODeviceSimpleInfo& info) {
    if(!deviceStatusCache_.isDoorLockLock(info))
        return DeviceOperationResult(-1,"门锁已被解锁");
    return deviceManageService_.unlockDoorLock(info);
}

// DeviceOperationResult DeviceService::controlCarRotation( const CarControl& car) {
//     return deviceManageService_.controlCarRotation(car);
// }

CameraRealTimeFrame DeviceService::getCameraRealTimeFrame( const CameraInfo& info) {
    return realTimeFrameCache_.getCameraRealTimeFrame(info);
}

CameraHistoryVideo DeviceService::viewCameraHistoryVideo( const CameraInfo& info) {
    return deviceManageService_.getCameraHistoryVideo(info);
}

// RadarPointCloud DeviceService::getRadarPointCloudData( const RadarInfo& info) {
//     return deviceManageService_.getRadarPointCloudData(info);
// }

// BoxConfigResult DeviceService::configBoxDeviceParams( const BoxDeviceParam& params) {
//     return deviceManageService_.boxDeviceParamsConfig(params);
// }

void DeviceService::startTimer() {
    _running = true;

    _timerThread = std::thread(&DeviceService::timerLoop,this);
}

void DeviceService::stopTimer() {
    _running = false;

    if(_timerThread.joinable()) _timerThread.join();
}

void DeviceService::devicesDataCollection(int deviceType) {
    
    // 设备数据采集 DeviceData 父类
    std::vector<DeviceData> deviceData = deviceManageService_.deviceDataAcquisition(deviceType);
    //更新一种设备的状态和实时数据
    deviceStatusCache_.updateDeviceStatus(deviceData);
}

void DeviceService::timerLoop() {
    using namespace std::chrono;
    while (_running) {
        auto start = steady_clock::now();
        
        // 获取所有采集任务，并直接调用 execute() 虚函数
        auto tasks = deviceAcquisitionTask_.getTasks();
        for (auto& task : tasks) {
            //task->execute();   // 多态调用，自动分发给具体子类
            auto collectedData = task->execute();   // 获取采集结果
            // if (!collectedData.empty()) {
            //     deviceStatusCache_.updateDeviceStatus(collectedData);
            // }
            if (!collectedData.empty()) {
                std::cout << "[DeviceService] 收到 " << collectedData.size() 
                          << " 条采集数据，准备更新缓存" << std::endl;
                deviceStatusCache_.updateDeviceStatus(collectedData);
                std::cout << "[DeviceService] 缓存更新完成" << std::endl;
            } else {
                std::cout << "[DeviceService] 本次采集未获取到数据" << std::endl;
            }
        }
        
        // 1 秒的采集周期
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
        //auto sleep_for = milliseconds(1000) - elapsed;
        //0.5s采集周期
        auto sleep_for = milliseconds(500) - elapsed;
        if (sleep_for > milliseconds(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }
}
// void DeviceService::timerLoop() {
//     using namespace std::chrono;
//     auto nextTick = steady_clock::now();

//     while(_running) {
//         nextTick += seconds(1);

//         executeTasks();

//         std::unique_lock<std::mutex> lock(_mutex);
//         _cv.wait_until(lock, nextTick, [this]{
//             return !_running;
//         });
//     }
// }
