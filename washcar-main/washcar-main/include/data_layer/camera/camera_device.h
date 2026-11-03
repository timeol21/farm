#ifndef CAMERA_DEVICE_H
#define CAMERA_DEVICE_H

#include <string>

class CameraDevice {
public:
    CameraDevice() = default;
    CameraDevice(std::string cameraId,
                 std::string name,
                 std::string rtspUrl);
    ~CameraDevice() = default;

    const std::string& getCameraId() const;
    const std::string& getName() const;
    const std::string& getRtspUrl() const;

private:
    std::string cameraId_;
    std::string name_;
    std::string rtspUrl_;
};

#endif
