#ifndef CAMERA_STATUS_H
#define CAMERA_STATUS_H

#include <string>
#include "data_layer/camera/camera_utils.h"
#include "data_layer/device/device_status.h"
class CameraStatus : public DeviceStatus{
    public:
        CameraStatus(const std::string& deviceId,
                     const int type,
                     const std::string& name,
                     const CameraOnlineStatus& status);
        CameraStatus() = default;
        ~CameraStatus() override = default;
        // Status getStatus() const;
        // const CameraStatus& getCameraStatus() const;

    private:

        CameraOnlineStatus status_;

};

#endif 