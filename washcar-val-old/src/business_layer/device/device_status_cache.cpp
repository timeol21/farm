#include "business_layer/device/device_status_cache.h"
#include <stdexcept>
#include <functional>
#include <mutex> 
DeviceStatusCache::DeviceStatusCache() {
    //solenoidStatusMap_.clear(); 
    //solenoidStatusMap_.reserve(16);
    cameraStatusMap_.clear();
    cameraStatusMap_.reserve(4);
    //infraredSensorStatusMap_.clear(); 
    //infraredSensorStatusMap_.reserve(4);
    //waterLevelSensorStatusMap_.clear(); 
    //waterLevelSensorStatusMap_.reserve(4);
    //smokeDetectorStatusMap_.clear(); 
    //smokeDetectorStatusMap_.reserve(4);
    fxPlcStatusMap_.clear();
    fxPlcStatusMap_.reserve(8);
}

DeviceStatusCache::~DeviceStatusCache() {

}

void DeviceStatusCache::updateBoxDeviceStatus(const BoxDeviceStatus& devices) {
    // const std::vector<SolenoidStatus>& solenoidStatusList = devices.getSolenoidStatusList();
    // for( auto& solenoidStatus : solenoidStatusList) {
    //     updateSolenoidStatus(solenoidStatus);
    // }

    const std::vector<CameraStatus>& cameraStatusList = devices.getCameraStatusList();
    for (auto& cameraStatus : cameraStatusList) {
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

    const std::vector<FxPlcStatus>& fxplcStatusList = devices.getFxPlcStatusList();
    for (auto& status : fxplcStatusList) {
        updateFxPlcStatus(status);
    }
}

void DeviceStatusCache::updateDeviceStatus(const std::vector<DeviceData>& deviceDataList) {
    if (deviceDataList.empty()) return;

    const DeviceData& deviceData = deviceDataList.front();

    switch (deviceData.getType()) {
        // case 0:
        //     for(auto& solenoid : deviceDataList)
        //         updateSolenoidStatus( solenoid.getSolenoidStatus()); 
        //     break;
    case 2:
        for (auto& camera : deviceDataList)
            updateCameraStatus(camera.getCameraStatus());
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
    case 7:{
        std::lock_guard<std::mutex> lock(mutex_);
            for (auto& fxplc : deviceDataList) {
                const FxPlcStatus& newStatus = fxplc.getFxPlcStatus();
                const std::string& plcId = newStatus.getDeviceId();
                auto it = fxPlcStatusMap_.find(plcId);

                if (it != fxPlcStatusMap_.end()) {
                    const FxPlcStatus& oldStatus = it->second;
                    bool hasChange = false;

                    //比较y点
                    for (const auto& [addr, newState] : newStatus.getYMap()) {
                        bool oldState = false;
                        bool oldExists = oldStatus.getYBit(addr, oldState);
                        if (!oldExists || oldState != newState) {
                            hasChange = true;
                            break;
                        }
                    }
                    if (!hasChange) {
                        //比较m点
                        for (const auto& [addr, newState] : newStatus.getMMap()) {
                            bool oldState = false;
                            bool oldExists = oldStatus.getMBit(addr, oldState);
                            if (!oldExists || oldState != newState) {
                                hasChange = true;
                                break;
                            }
                        }
                    }
                    if (!hasChange) {
                        //比较d寄存器
                        for (const auto& [addr, newVal] : newStatus.getDMap()) {
                            uint16_t oldVal = 0;
                            bool oldExists = oldStatus.getDRegister(addr, oldVal);
                            if (!oldExists || oldVal != newVal) {
                                hasChange = true;
                                break;
                            }
                        }
                    }
                    if (!hasChange) {
                        //比较x点
                        for (const auto& [addr, newState] : newStatus.getXMap()) {
                            bool oldState = false;
                            bool oldExists = oldStatus.getXBit(addr, oldState);
                            if (!oldExists || oldState != newState) {
                                hasChange = true;
                                break;
                            }
                        }
                    }
                    if (!hasChange) {
                        //比较s点
                        for (const auto& [addr, newState] : newStatus.getSMap()) {
                            bool oldState = false;
                            bool oldExists = oldStatus.getSBit(addr, oldState);
                            if (!oldExists || oldState != newState) {
                                hasChange = true;
                                break;
                            }
                        }
                    }
                    //合并
                    it->second.updateFrom(newStatus);

                    if (hasChange) {
                        it->second.setChanged();
                    }
                } 
                // else {
                //     //待扩展
                //     FxPlcStatus newCopy = newStatus;
                //     newCopy.setChanged();
                //     fxPlcStatusMap_[plcId] = std::move(newCopy);
                // }
            }
            break;
        }
    default:
        break;
    }

}
std::vector<FxPlcStatus> DeviceStatusCache::getChangedFxPlcStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FxPlcStatus> changedList;
    for (auto& [plcId, status] : fxPlcStatusMap_) {
        if (status.isChanged()) {
            changedList.push_back(status);   //拷贝当前状态
        }
    }
    return changedList;
}
void DeviceStatusCache::clearChangedForPlc(const std::string& plcId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fxPlcStatusMap_.find(plcId);
    if (it != fxPlcStatusMap_.end()) {
        it->second.clearChanged();
    }
}

void DeviceStatusCache::clearChangedForPlcs(const std::vector<std::string>& plcIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& plcId : plcIds) {
        auto it = fxPlcStatusMap_.find(plcId);
        if (it != fxPlcStatusMap_.end()) {
            it->second.clearChanged();
        }
    }
}

BoxDeviceStatus DeviceStatusCache::getBoxDeviceStatus() {
    // std::vector<SolenoidStatus> solenoidStatusList;
    // solenoidStatusList.reserve(solenoidStatusMap_.size());
    // for( auto& [id, solenoid] : solenoidStatusMap_) {
    //     solenoidStatusList.push_back(solenoid );
    // }

    std::vector<CameraStatus> cameraStatusList;
    for (auto& [id, camera] : cameraStatusMap_) {
        cameraStatusList.push_back(camera);
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
    std::vector<FxPlcStatus> fxPlcStatusList;
    for (const auto& [id, status] : fxPlcStatusMap_) {
        fxPlcStatusList.push_back(status);
    }
    return BoxDeviceStatus(cameraStatusList, fxPlcStatusList);

}

bool DeviceStatusCache::isBoxDeviceStatusEmpty() {
    //return solenoidStatusMap_.empty() && 
    return cameraStatusMap_.empty() && fxPlcStatusMap_.empty();
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


/*void DeviceStatusCache::updateSolenoidStatus( const SolenoidStatus& status ) {
    const std::string& deviceId = status.getDeviceId();

    auto it = solenoidStatusMap_.find(deviceId);
    if(it == solenoidStatusMap_.end())
        solenoidStatusMap_[deviceId] = status;
    else it->second = status;
}*/


void DeviceStatusCache::updateCameraStatus(const CameraStatus& status) {

    const std::string& deviceId = status.getDeviceId();

    auto it = cameraStatusMap_.find(deviceId);
    if (it == cameraStatusMap_.end())
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
void DeviceStatusCache::updateFxPlcStatus(const FxPlcStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string& plcId = status.getDeviceId();
    auto it = fxPlcStatusMap_.find(plcId);
    if (it == fxPlcStatusMap_.end()) {
        fxPlcStatusMap_[plcId] = status;
    } else {
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
bool DeviceStatusCache::getFxPlcStatus(const std::string& plcId, FxPlcStatus& outStatus) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fxPlcStatusMap_.find(plcId);
    if (it == fxPlcStatusMap_.end()) return false;
    outStatus = it->second;
    return true;
}

// bool DeviceStatusCache::isBoxDeviceStatusEmpty() const {
//     return cameraStatusMap_.empty() && fxPlcStatusMap_.empty();
// }

