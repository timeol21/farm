#include "business_layer/device/device_service.h"
#include<optional>
#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "common/log/log_manager.h"
#include "common/config/config_manager.h"
#include "common/config/point.h"

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
}

DeviceService::~DeviceService() = default;

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
    bool ok = deviceManageService_.forceM(plcId, mDecimal, turnOn);
    // 关键点位写结果：洗车命令链常用 m0(停止)/m100(复位)/m1023(测试)/m500(点动)/m616 等
    bool isKey = (mDecimal == 0 || mDecimal == 100 || mDecimal == 500 || mDecimal == 1023);
    if (isKey || !ok) {
        LOG_INFO(std::string("[DeviceService] PLC_WRITE_RESULT point=m") + std::to_string(mDecimal)
            + ", plcId=" + plcId
            + ", val=" + (turnOn ? "1" : "0")
            + ", result=" + (ok ? "ok" : "FAIL"));
    }
    return ok;
}
bool DeviceService::forceFxX(const std::string& plcId, int xOctal, bool turnOn) {
    bool ok = deviceManageService_.forceX(plcId, xOctal, turnOn);
    // 关键点：X45=启动；其他失败/异常也记
    bool isKey = (xOctal == 45);
    if (isKey || !ok) {
        LOG_INFO(std::string("[DeviceService] PLC_WRITE_RESULT point=x") + std::to_string(xOctal)
            + ", plcId=" + plcId
            + ", val=" + (turnOn ? "1" : "0")
            + ", result=" + (ok ? "ok" : "FAIL"));
    }
    return ok;
}
std::vector<FxPlcStatus> DeviceService::getChangedFxPlcStatus() {
    return deviceStatusCache_.getChangedFxPlcStatus();
}
void DeviceService::clearChangedFxPlcStatus(const std::vector<std::string>& plcIds) {
    deviceStatusCache_.clearChangedForPlcs(plcIds);
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

bool DeviceService::readPointNow(const std::string& plcId, const std::string& point,
                                 int& value, std::string& error) {
    auto& plcSet = deviceManageService_.getFxPlcSet();
    IPlcInstance* plc = nullptr;
    if (!plcId.empty()) {
        plc = plcSet.getPlc(plcId);
    } else {
        const auto& all = plcSet.getAll();
        if (!all.empty()) plc = all.begin()->second.get();
    }
    if (plc == nullptr) {
        error = "PLC 不存在: " + (plcId.empty() ? std::string("<default>") : plcId);
        return false;
    }
    if (!plc->connect()) {
        error = "PLC 连接失败";
        return false;
    }

    const auto parsed = washcar::cfg::parsePoint(point);
    if (parsed.type == '?') {
        error = "点位格式非法: " + point;
        return false;
    }
    bool bit = false;
    switch (parsed.type) {
    case 'D': {
        uint16_t word = 0;
        if (!plc->readD(parsed.addr, word)) { error = "读 D" + std::to_string(parsed.addr) + " 失败"; return false; }
        value = static_cast<int>(word);
        return true;
    }
    case 'M':
        if (!plc->readM(parsed.addr, bit)) { error = "读 M" + std::to_string(parsed.addr) + " 失败"; return false; }
        value = bit ? 1 : 0;
        return true;
    case 'X':
        if (!plc->readXBit(parsed.addr, bit)) { error = "读 X" + std::to_string(parsed.addr) + " 失败"; return false; }
        value = bit ? 1 : 0;
        return true;
    case 'Y':
        if (!plc->readYBit(parsed.addr, bit)) { error = "读 Y" + std::to_string(parsed.addr) + " 失败"; return false; }
        value = bit ? 1 : 0;
        return true;
    case 'S':
        if (!plc->readS(parsed.addr, bit)) { error = "读 S" + std::to_string(parsed.addr) + " 失败"; return false; }
        value = bit ? 1 : 0;
        return true;
    default:
        error = "不支持的点位类型";
        return false;
    }
}

void DeviceService::devicesDataCollection(int deviceType) {
    
    // 设备数据采集 DeviceData 父类
    std::vector<DeviceData> deviceData = deviceManageService_.deviceDataAcquisition(deviceType);
    //更新一种设备的状态和实时数据
    deviceStatusCache_.updateDeviceStatus(deviceData);
}

// 采集一轮: 由大厅定时器按 timing.acquisitionIntervalMs 周期调(以前这里自带 sleep 循环)
void DeviceService::acquisitionOnce() {
    auto tasks = deviceAcquisitionTask_.getTasks();
    for (auto& task : tasks) {
        auto collectedData = task->execute();
        if (!collectedData.empty()) {
            deviceStatusCache_.updateDeviceStatus(collectedData);
        } else if (ConfigManager::instance().box().plcEnabled) {
            std::cout << "[DeviceService] 本次采集未获取到数据" << std::endl;
        }
    }
}
