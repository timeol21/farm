#include "data_layer/box/box_device_status.h"

BoxDeviceStatus::BoxDeviceStatus(
  //const std::vector<SolenoidStatus> solenoidStatuses,
                                const std::vector<CameraStatus> cameraStatuses,
                                //const std::vector<InfraredSensorStatus> infraredSensorStatuses,
                                //const std::vector<PlcSmokeDetectorStatus> smokeDetectorStatuses,
                                //const std::vector<PlcWaterLevelSensorStatus> waterSensorStatuses,
                                const std::vector<FxPlcStatus>& fxPlcStatusList)
    : 
    //solenoidStatuses_(solenoidStatuses),
      cameraStatuses_(cameraStatuses),
      //infraredSensorStatuses_(infraredSensorStatuses),
      //smokeDetectorStatuses_(smokeDetectorStatuses),
      //waterSensorStatuses_(waterSensorStatuses),
      fxPlcStatusList_(fxPlcStatusList) {

}

// std::vector<SolenoidStatus> BoxDeviceStatus::getSolenoidStatusList() const {
//   return solenoidStatuses_;
// }

std::vector<CameraStatus> BoxDeviceStatus::getCameraStatusList() const {
  return cameraStatuses_;
}

// std::vector<InfraredSensorStatus> BoxDeviceStatus::getInfraredSensorStatusList() const {
//   return infraredSensorStatuses_;
// }
// std::vector<PlcSmokeDetectorStatus> BoxDeviceStatus::getSmokeDetectorStatusList() const {
//   return smokeDetectorStatuses_;
// }
// std::vector<PlcWaterLevelSensorStatus> BoxDeviceStatus::getWaterLevelSensorStatusList() const {
//   return waterSensorStatuses_;
// }

const std::vector<FxPlcStatus>& BoxDeviceStatus::getFxPlcStatusList() const {
    return fxPlcStatusList_;
}