#include "data_layer/camera/camera_instance_set.h"

CameraInstanceSet::CameraInstanceSet(std::unordered_map<std::string, std::unique_ptr<Camera>>&& cameras) {
    // for(auto& [key, cam] : cameras) {
    //     cameras_[key] = std::move(cam);
    // }
    cameras_ = std::move(cameras);
}

CameraInstanceSet::~CameraInstanceSet() {

}

void CameraInstanceSet::addCamera(const std::string& cameraId, std::unique_ptr<Camera> camera) {
    cameras_[cameraId] = std::move(camera);
}

Camera* CameraInstanceSet::getCamera(const std::string& cameraId) {
    auto it = cameras_.find(cameraId);
    if (it != cameras_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const std::unordered_map<std::string, std::unique_ptr<Camera>>& CameraInstanceSet::getAll() const {
    return cameras_;
}

std::vector<CameraStatus> CameraInstanceSet::getCameraStatusList() {
    std::vector<CameraStatus> cameraStatusList;
    cameraStatusList.reserve(cameras_.size());
    for (auto& [key, camera] : cameras_) {
        cameraStatusList.push_back(camera->getStatus());
    }
    return cameraStatusList;
}

bool CameraInstanceSet::getCameraStatus(const std::string& cameraId, CameraStatus& out) {
    auto it = cameras_.find(cameraId);
    if (it == cameras_.end() || !it->second) {
        return false;
    }
    out = it->second->getStatus();
    return true;
}

CameraHistoryVideo CameraInstanceSet::getCameraHistoryVideo(const std::string& cameraId) {
    auto camera = cameras_.find(cameraId);
    if (camera == cameras_.end()) {
        return CameraHistoryVideo();
    }
    return camera->second->getHistoryVideo();
}

std::vector<DeviceData> CameraInstanceSet::acquisitionCameraData() {
    std::vector<DeviceData> cameraDataList;
    cameraDataList.reserve(cameras_.size());
    for (auto& [cameraId, camera] : cameras_) {
        CameraStatus status = camera->getStatus();
        cameraDataList.push_back(DeviceData(2, status));
    }
    return cameraDataList;
}