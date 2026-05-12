#ifndef CAMERA_INSTANCE_SET_H
#define CAMERA_INSTANCE_SET_H

#include "data_layer/camera/camera_status.h"
#include "data_layer/camera/camera.h"
#include "data_layer/camera/camera_history_video.h"
#include "data_layer/device/device_data.h"
#include <unordered_map>
#include <memory>
#include <vector>

class CameraInstanceSet {
    public:
        CameraInstanceSet() = default;
        CameraInstanceSet(std::unordered_map<std::string, std::unique_ptr<Camera>>&& cameras);
        ~CameraInstanceSet();
        CameraInstanceSet(CameraInstanceSet&&) = default;
        CameraInstanceSet& operator=(CameraInstanceSet&&) = default;

        CameraInstanceSet(const CameraInstanceSet&) = delete;
        CameraInstanceSet& operator=(const CameraInstanceSet&) = delete;

        std::vector<CameraStatus> getCameraStatusList() ;

        CameraHistoryVideo getCameraHistoryVideo(const std::string& cameraId) ;

        std::vector<DeviceData> acquisitionCameraData();

    private:
        std::unordered_map<std::string, std::unique_ptr<Camera> > cameras_; 
};

#endif