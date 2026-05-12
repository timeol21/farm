#include "data_layer/device/device_data.h"

// DeviceData::DeviceData(int type,const SolenoidStatus solenoidStatus) 
//     : type(type), solenoidStatus_(solenoidStatus){

// }

DeviceData::DeviceData(int type,const TempHumidSensorStatus sensorStatus) 
    : type(type), sensorStatus_(sensorStatus) {

}

DeviceData::DeviceData(int type,const CameraStatus cameraStatus) 
    : type(type), cameraStatus_(cameraStatus) {

}

DeviceData::DeviceData(int type,const DoorLockStatus doorLockStatus) 
    : type(type), doorLockStatus_(doorLockStatus) {

}
// DeviceData::DeviceData(int type,const InfraredSensorStatus infraredSensorStatus) 
//     : type(type), infraredSensorStatus_(infraredSensorStatus) {

// }
// DeviceData::DeviceData(int type,const PlcSmokeDetectorStatus smokeDetectorStatus) 
//     : type(type), smokeDetectorStatus_(smokeDetectorStatus) {

// }
// DeviceData::DeviceData(int type,const PlcWaterLevelSensorStatus waterLevelSensorStatus) 
//     :type(type), waterLevelSensorStatus_(waterLevelSensorStatus) {

// }

int DeviceData::getType() const {
    return type;
}

// SolenoidStatus DeviceData::getSolenoidStatus() const {
//     return solenoidStatus_;
// }
 

TempHumidSensorStatus DeviceData::getSensorStatus() const {
    return sensorStatus_;
}   

CameraStatus DeviceData::getCameraStatus() const {
    return cameraStatus_;
}

DoorLockStatus DeviceData::getDoorLockStatus() const {
    return doorLockStatus_;
}
// InfraredSensorStatus DeviceData::getInfraredSensorStatus() const {
//     return infraredSensorStatus_;
// }
// PlcSmokeDetectorStatus DeviceData::getPlcSmokeDetectorStatus() const {
//     return smokeDetectorStatus_;
// }
// PlcWaterLevelSensorStatus DeviceData::getPlcWaterLevelSensorStatus() const {
//     return waterLevelSensorStatus_;
// }

DeviceData::DeviceData(int type, const FxPlcStatus& fxPlcStatus)
    : type(type), fxPlcStatus_(fxPlcStatus) {}

// getter 实现
FxPlcStatus DeviceData::getFxPlcStatus() const {
    return fxPlcStatus_;
}