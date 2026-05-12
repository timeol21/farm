#include "data_layer/camera/camera_info.h"

CameraInfo::CameraInfo(std::string deviceId) : deviceId_(deviceId) {

}


const std::string& CameraInfo::getDeviceId() const {
    return deviceId_;
}