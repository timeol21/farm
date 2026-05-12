#include "data_layer/camera/camera_status.h"

CameraStatus::CameraStatus(const std::string& deviceId,
                           const int type,
                           const std::string& name,
                           const CameraOnlineStatus& status)
    : DeviceStatus(deviceId,type,name),
      status_(status) {

}


// Status CameraStatus::getStatus() const{
//     return status_;
// }

// const CameraStatus& CameraStatus::getCameraStatus() const {
//     return *this;
// }