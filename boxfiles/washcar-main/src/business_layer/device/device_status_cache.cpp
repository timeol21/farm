#include "business_layer/device/device_status_cache.h"
#include <stdexcept>
#include <functional>
DeviceStatusCache::DeviceStatusCache() {
    //solenoidStatusMap_.clear(); 
    //solenoidStatusMap_.reserve(16);
    sensorStatusMap_.clear();   
    sensorStatusMap_.reserve(8);
    cameraStatusMap_.clear();   
    cameraStatusMap_.reserve(4);
    //infraredSensorStatusMap_.clear(); 
    //infraredSensorStatusMap_.reserve(4);
    //waterLevelSensorStatusMap_.clear(); 
    //waterLevelSensorStatusMap_.reserve(4);
    //smokeDetectorStatusMap_.clear(); 
    //smokeDetectorStatusMap_.reserve(4);
    doorLockStatusMap_.clear(); 
    doorLockStatusMap_.reserve(4);
    fxPlcStatusMap_.clear();
    fxPlcStatusMap_.reserve(8);
}

DeviceStatusCache::~DeviceStatusCache() {

}

void DeviceStatusCache::updateBoxDeviceStatus( const BoxDeviceStatus& devices) {
    // const std::vector<SolenoidStatus>& solenoidStatusList = devices.getSolenoidStatusList();
    // for( auto& solenoidStatus : solenoidStatusList) {
    //     updateSolenoidStatus(solenoidStatus);
    // }

    const std::vector<TempHumidSensorStatus> sensorStatusList = devices.getSensorStatusList();
    for( auto& sensorStatus : sensorStatusList) {
        updateTempHumidSensorStatus(sensorStatus);
    }
    const std::vector<CameraStatus>& cameraStatusList = devices.getCameraStatusList();
    for( auto& cameraStatus : cameraStatusList) {
        updateCameraStatus(cameraStatus);
    }

    // const std::vector<InfraredSensorStatus>& infraredSensorStatusList = devices.getInfraredSensorStatusList();
    // for( auto& infraredSensorStatus : infraredSensorStatusList) {
    //     updateInfraredSensorStatus(infraredSensorStatus);
    // }

    // const std::vector<PlcWaterLevelSensorStatus>& waterLevelSensorStatusList = devices.getWaterLevelSensorStatusList();
    // for( auto& sensorStatus : waterLevelSensorStatusList) {
    //     updatePlcWaterLevelSensorStatus(sensorStatus);
    // }

    // const std::vector<PlcSmokeDetectorStatus>& smokeDetectorStatusList = devices.getSmokeDetectorStatusList();
    // for( auto& status : smokeDetectorStatusList) {
    //     updatePlcSmokeDetectorStatus(status);
    // }

    const std::vector<DoorLockStatus>& doorLockStatusList = devices.getDoorLockStatusList();
    for( auto& status : doorLockStatusList) {
        updateDoorLockStatus(status);
    }
    //空传
    const std::vector<FxPlcStatus>& fxplcStatusList = devices.getFxPlcStatusList();
    for(auto& status : fxplcStatusList){
        updateFxPlcStatus(status);
    }
}

void DeviceStatusCache::updateDeviceStatus( const std::vector<DeviceData>& deviceDataList ) {
    if(deviceDataList.empty() ) return ;

    const DeviceData& deviceData = deviceDataList.front();

    switch(deviceData.getType()) {
        // case 0:
        //     for(auto& solenoid : deviceDataList)
        //         updateSolenoidStatus( solenoid.getSolenoidStatus()); 
        //     break;
        case 1: 
            for(auto& sensor : deviceDataList)
                updateTempHumidSensorStatus(sensor.getSensorStatus());
            break;
        case 2:
            for(auto& camera : deviceDataList)
                updateCameraStatus(camera.getCameraStatus());
            break;
        case 3:
            for(auto& doorLock : deviceDataList)
                updateDoorLockStatus(doorLock.getDoorLockStatus());
            break;
        // case 4:
        //     for(auto& infraredSensor : deviceDataList)
        //         updateInfraredSensorStatus(infraredSensor.getInfraredSensorStatus());
        //     break;
        // case 5:
        //     for(auto& smokeDetector : deviceDataList)
        //         updatePlcSmokeDetectorStatus(smokeDetector.getPlcSmokeDetectorStatus());
        //     break;
        // case 6:
        //     for(auto& waterLevelSensor : deviceDataList)
        //         updatePlcWaterLevelSensorStatus(waterLevelSensor.getPlcWaterLevelSensorStatus());
        //     break;
        case 7:
            for(auto& fxplc : deviceDataList){
                const FxPlcStatus& newStatus = fxplc.getFxPlcStatus();
                const std::string& plcId = newStatus.getDeviceId();
                auto it = fxPlcStatusMap_.find(plcId);
                bool hasOld = (it != fxPlcStatusMap_.end());
                FxPlcStatus oldStatus;
                if (hasOld) oldStatus = it->second;
                char buf[32];
                // ---- 比较 Y 点 ----
                for (const auto& [addr, newState] : newStatus.getYMap()) {
                    bool oldState = false;
                    bool oldExists = hasOld && oldStatus.getYBit(addr, oldState);
                    if (!oldExists && oldState != newState) {
                        ChangeRecord cr;
                        cr.deviceId = plcId;
                        //cr.pointName = "Y" + std::to_string(std::oct, addr);  
                        sprintf(buf, "Y%o", addr);  
                        cr.pointName = buf;
                        cr.oldValue = oldExists ? (oldState ? "1" : "0") : "?";
                        cr.newValue = newState ? "1" : "0";
                        cr.deviceType = 7;
                        notifyListeners(cr);
                    }
                }

                // ---- 比较 M 点 ----
                for (const auto& [addr, newState] : newStatus.getMMap()) {
                    bool oldState = false;
                    bool oldExists = hasOld && oldStatus.getMBit(addr, oldState);
                    if (!oldExists && oldState != newState) {
                        ChangeRecord cr;
                        cr.deviceId = plcId;
                        cr.pointName = "M" + std::to_string(addr);
                        cr.oldValue = oldExists ? (oldState ? "1" : "0") : "?";
                        cr.newValue = newState ? "1" : "0";
                        cr.deviceType = 7;
                        notifyListeners(cr);
                    }
                }

                // ---- 比较 D 寄存器 ----
                for (const auto& [addr, newVal] : newStatus.getDMap()) {
                    uint16_t oldVal = 0;
                    bool oldExists = hasOld && oldStatus.getDRegister(addr, oldVal);
                    if (!oldExists && oldVal != newVal) {
                        ChangeRecord cr;
                        cr.deviceId = plcId;
                        cr.pointName = "D" + std::to_string(addr);
                        cr.oldValue = oldExists ? std::to_string(oldVal) : "?";
                        cr.newValue = std::to_string(newVal);
                        cr.deviceType = 7;
                        notifyListeners(cr);
                    }
                }

                // ---- 比较 X 点 ----
                for (const auto& [addr, newState] : newStatus.getXMap()) {
                    bool oldState = false;
                    bool oldExists = hasOld && oldStatus.getXBit(addr, oldState);
                    if (!oldExists && oldState != newState) {
                        ChangeRecord cr;
                        cr.deviceId = plcId;
                        //cr.pointName = "X" + std::to_string(std::oct, addr);
                        sprintf(buf, "X%o", addr);
                        cr.pointName = buf;
                        cr.oldValue = oldExists ? (oldState ? "1" : "0") : "?";
                        cr.newValue = newState ? "1" : "0";
                        cr.deviceType = 7;
                        notifyListeners(cr);
                    }
                }

                // ---- 比较 S 点 ----
                for (const auto& [addr, newState] : newStatus.getSMap()) {
                    bool oldState = false;
                    bool oldExists = hasOld && oldStatus.getSBit(addr, oldState);
                    if (!oldExists && oldState != newState) {
                        ChangeRecord cr;
                        cr.deviceId = plcId;
                        cr.pointName = "S" + std::to_string(addr);
                        cr.oldValue = oldExists ? (oldState ? "1" : "0") : "?";
                        cr.newValue = newState ? "1" : "0";
                        cr.deviceType = 7;
                        notifyListeners(cr);
                    }
                }

                // 所有比较完成后，再执行原有的更新（这里直接用赋值替换）
                fxPlcStatusMap_[plcId] = newStatus;
            }
            break;
             
        default:
            break;
    }

}

BoxDeviceStatus DeviceStatusCache::getBoxDeviceStatus() {
    // std::vector<SolenoidStatus> solenoidStatusList;
    // solenoidStatusList.reserve(solenoidStatusMap_.size());
    // for( auto& [id, solenoid] : solenoidStatusMap_) {
    //     solenoidStatusList.push_back(solenoid );
    // }

    std::vector<TempHumidSensorStatus> sensorStatusList;
    for( auto& [id, sensor] : sensorStatusMap_) {
        sensorStatusList.push_back( sensor );
    }

    std::vector<CameraStatus> cameraStatusList;
    for( auto& [id, camera] : cameraStatusMap_) {
        cameraStatusList.push_back( camera );
    }

    std::vector<DoorLockStatus> doorLockStatusList;
    for( auto& [id, doorLock] : doorLockStatusMap_) {
        doorLockStatusList.push_back( doorLock );
    }

    // std::vector<InfraredSensorStatus> infraredSensorStatusList;
    // for( auto& [id, sensor] : infraredSensorStatusMap_) {
    //     infraredSensorStatusList.push_back( sensor );
    // }

    // std::vector<PlcSmokeDetectorStatus> smokeDetectorStatusList;
    // for( auto& [id, detector] : smokeDetectorStatusMap_) {
    //     smokeDetectorStatusList.push_back( detector );
    // }

    // std::vector<PlcWaterLevelSensorStatus> waterLevelStatusList;
    // for( auto& [id, sensor] : waterLevelSensorStatusMap_) {
    //     waterLevelStatusList.push_back( sensor );
    // }

    //return BoxDeviceStatus(solenoidStatusList, cameraStatusList, sensorStatusList,infraredSensorStatusList,smokeDetectorStatusList,waterLevelStatusList,doorLockStatusList);
    //目前plc为空列表
    std::vector<FxPlcStatus> fxPlcStatusList;
    for (const auto& [id, status] : fxPlcStatusMap_) {
        fxPlcStatusList.push_back(status);
    }
    return BoxDeviceStatus(cameraStatusList, sensorStatusList,doorLockStatusList, fxPlcStatusList);

}

bool DeviceStatusCache::isBoxDeviceStatusEmpty() {
    //return solenoidStatusMap_.empty() && 
    return sensorStatusMap_.empty() && cameraStatusMap_.empty()  && fxPlcStatusMap_.empty();
}

/*bool DeviceStatusCache::isSolenoidOpen( const PlcDeviceInfo& info) {
    auto it = solenoidStatusMap_.find(info.getDeviceId());

    if(it == solenoidStatusMap_.end())
        throw std::runtime_error("Solenoid device not found in cache");

    SolenoidStatus& status = it->second;
    return status.isOpen();
}

bool DeviceStatusCache::isSolenoidClose( const PlcDeviceInfo& info) {
    auto it = solenoidStatusMap_.find(info.getDeviceId());

    if(it == solenoidStatusMap_.end())
        throw std::runtime_error("Solenoid device not found in cache");

    SolenoidStatus& status = it->second;
    return !status.isOpen();
}*/


bool DeviceStatusCache::isDoorLockLock(const GPIODeviceSimpleInfo& info) {
    auto it = doorLockStatusMap_.find(info.getDeviceId());

    if(it == doorLockStatusMap_.end())
        throw std::runtime_error("Solenoid device not found in cache");

    DoorLockStatus& status = it->second;
    return status.isLock();
}
/*void DeviceStatusCache::updateSolenoidStatus( const SolenoidStatus& status ) {
    const std::string& deviceId = status.getDeviceId();

    auto it = solenoidStatusMap_.find(deviceId);
    if(it == solenoidStatusMap_.end()) 
        solenoidStatusMap_[deviceId] = status;
    else it->second = status;
}*/


void DeviceStatusCache::updateTempHumidSensorStatus( const TempHumidSensorStatus& status) {

    const std::string& deviceId = status.getDeviceId();
    auto it = sensorStatusMap_.find(deviceId);
    if(it == sensorStatusMap_.end())
        sensorStatusMap_[deviceId] = status;
    else it->second = status;
}

void DeviceStatusCache::updateCameraStatus( const CameraStatus& status) {

    const std::string& deviceId = status.getDeviceId();

    auto it = cameraStatusMap_.find(deviceId);
    if(it == cameraStatusMap_.end())
        cameraStatusMap_[deviceId] = status;
    else it->second = status;
}
/*
void DeviceStatusCache::updateInfraredSensorStatus(const InfraredSensorStatus& status) {
    const std::string& deviceId = status.getDeviceId();

    auto it = infraredSensorStatusMap_.find(deviceId);
    if(it == infraredSensorStatusMap_.end())
        infraredSensorStatusMap_[deviceId] = status;
    else it->second = status;
 }
void DeviceStatusCache::updatePlcWaterLevelSensorStatus(const PlcWaterLevelSensorStatus& status) {
    const std::string& deviceId = status.getDeviceId();

    auto it = waterLevelSensorStatusMap_.find(deviceId);
    if(it == waterLevelSensorStatusMap_.end())
        waterLevelSensorStatusMap_[deviceId] = status;
    else it->second = status;
}
void DeviceStatusCache::updatePlcSmokeDetectorStatus(const PlcSmokeDetectorStatus& status) {
    const std::string& deviceId = status.getDeviceId();

    auto it = smokeDetectorStatusMap_.find(deviceId);
    if(it == smokeDetectorStatusMap_.end())
        smokeDetectorStatusMap_[deviceId] = status;
    else it->second = status;
}
    */
void DeviceStatusCache::updateDoorLockStatus(const DoorLockStatus& status) {
    const std::string& deviceId = status.getDeviceId();

    auto it = doorLockStatusMap_.find(deviceId);
    if(it == doorLockStatusMap_.end())
        doorLockStatusMap_[deviceId] = status;
    else it->second = status;
}

//无锁
void DeviceStatusCache::updateFxPlcStatus(const FxPlcStatus& status) {
        const std::string& plcId = status.getDeviceId();
    auto it = fxPlcStatusMap_.find(plcId);
    if (it == fxPlcStatusMap_.end()) {
        fxPlcStatusMap_[plcId] = status;   
    } else {
        // FxPlcStatus& existing = it->second;
        // existing.updateFrom(status);
        it->second.updateFrom(status);
    }
    // const std::string& plcId = status.getDeviceId();   
    // auto it = fxPlcStatusMap_.find(plcId);
    // if(it == fxPlcStatusMap_.end()) {
    //     fxPlcStatusMap_[plcId] = status;
    // } else {
    //     it->second = status;
    // }
}
// bool DeviceStatusCache::getFxPlcStatus(const std::string& plcId, FxPlcStatus& outStatus) const {
//     auto it = fxPlcStatusMap_.find(plcId);
//     if(it != fxPlcStatusMap_.end()) {
//         outStatus = it->second;
//         return true;
//     }
//     return false;
// }

// bool DeviceStatusCache::isBoxDeviceStatusEmpty() const {
//     return sensorStatusMap_.empty() && cameraStatusMap_.empty() && fxPlcStatusMap_.empty();
// }


//变化检测监听
void DeviceStatusCache::addListener(std::shared_ptr<IDeviceChangeListener> listener) {
    listeners_.push_back(listener);
}

void DeviceStatusCache::removeListener(std::shared_ptr<IDeviceChangeListener> listener) {
    listeners_.remove_if([&](const std::weak_ptr<IDeviceChangeListener>& wp) {
        auto sp = wp.lock();
        return !sp || sp == listener;
    });
}

void DeviceStatusCache::notifyListeners(const ChangeRecord& change) {
    // 遍历并清除已失效的弱引用
    auto it = listeners_.begin();
    while (it != listeners_.end()) {
        auto listener = it->lock();
        if (listener) {
            listener->onDeviceChanged(change);
            ++it;
        } else {
            it = listeners_.erase(it);
        }
    }
}