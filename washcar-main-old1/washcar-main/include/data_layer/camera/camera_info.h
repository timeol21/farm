#ifndef CAMERA_INFO_H
#define CAMERA_INFO_H

#include <string>

class CameraInfo {
    
    public:
        CameraInfo() = default;
        CameraInfo(std::string deviceId);
        ~CameraInfo() = default;
    
        const std::string& getDeviceId() const;
    private:
        std::string deviceId_;
};

#endif