#ifndef CAMERA_STATUS_H
#define CAMERA_STATUS_H

#include <string>
#include "data_layer/camera/camera_utils.h"
#include "data_layer/device/device_status.h"
class CameraStatus : public DeviceStatus {
public:
    CameraStatus() = default;
    CameraStatus(const std::string& deviceId,
                const int type,
                const std::string& name,
                const CameraOnlineStatus& status);
    ~CameraStatus() override = default;

    CameraOnlineStatus getStatus() const { return status_; }
    void setStatus(CameraOnlineStatus status) { status_ = status; }

private:
    CameraOnlineStatus status_;
};

#endif 