#include "data_layer/camera/camera_status.h"

CameraStatus::CameraStatus(const std::string& deviceId,
                          const int type,
                          const std::string& name,
                          const CameraOnlineStatus& status)
    : DeviceStatus(deviceId, type, name),
      status_(status) {
}