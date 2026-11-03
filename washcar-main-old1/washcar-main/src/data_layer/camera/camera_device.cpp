#include "data_layer/camera/camera_device.h"

CameraDevice::CameraDevice(std::string cameraId,
                           std::string name,
                           std::string rtspUrl)
    : cameraId_(std::move(cameraId)),
      name_(std::move(name)),
      rtspUrl_(std::move(rtspUrl)) {
}

const std::string& CameraDevice::getCameraId() const {
    return cameraId_;
}

const std::string& CameraDevice::getName() const {
    return name_;
}

const std::string& CameraDevice::getRtspUrl() const {
    return rtspUrl_;
}
