#include "data_layer/camera/camera.h"

Camera::Camera(std::string cameraId, std::string name, std::string rtspUrl) 
    : cameraId_(cameraId), name_(name), rtspUrl_(rtspUrl), onlineStatus_(CameraOnlineStatus::OFFLINE) 
{
    std::lock_guard<std::mutex> lock(sattusMutex_);
    onlineStatus_ = CameraOnlineStatus::OFFLINE;
}

Camera::~Camera() {
    
}

bool Camera::start() {
    return true;
}

bool Camera::stop() {
    return true;
}

bool Camera::isRunning() const {
    return true;
}

bool Camera::getLastKeyFrame(FrameData& out) {
    return true;
}

CameraHistoryVideo Camera::getCameraHistoryVideo() {
    return CameraHistoryVideo();
}

CameraStatus Camera::getStatus() {
    return CameraStatus();
}

void Camera::pullKeyFrameLoop() {

}