#include "business_layer/device/real_time_frame_cache.h"
#include "common/log/log_manager.h"
#include <chrono>

RealTimeFrameCache::RealTimeFrameCache() {
}

void RealTimeFrameCache::onFrame(const std::string& cameraId, const FrameData& frame) {
    // 行为与原 Camera 直接调用 updateCameraRealTimeFrame 完全一致
    updateCameraRealTimeFrame(cameraId, frame);
}

void RealTimeFrameCache::updateCameraRealTimeFrame(const std::string& cameraId, const FrameData& frameData) {
    std::lock_guard<std::mutex> lock(mutex_);
    CameraRealTimeFrame realtimeFrame(frameData);
    _cameraFrameMap[cameraId] = realtimeFrame;
}

CameraRealTimeFrame RealTimeFrameCache::getCameraRealTimeFrame(const CameraInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = _cameraFrameMap.find(info.getDeviceId());
    if (it == _cameraFrameMap.end()) {
        return CameraRealTimeFrame();
    }
    return it->second;
}

void RealTimeFrameCache::setFramePullInterval(const std::string& cameraId, int intervalMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    _framePullIntervals[cameraId] = intervalMs;
}

void RealTimeFrameCache::setIdleMode(const std::string& cameraId) {
    setFramePullInterval(cameraId, 10000);  // 空闲模式：10秒
}

void RealTimeFrameCache::setWorkingMode(const std::string& cameraId) {
    setFramePullInterval(cameraId, 5000);  // 工作模式：5秒
}

bool RealTimeFrameCache::shouldPullFrame(const std::string& cameraId) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = _framePullIntervals.find(cameraId);
    int interval = (it != _framePullIntervals.end()) ? it->second : 10000;

    auto lastIt = _lastPullTime.find(cameraId);
    int64_t lastTime = (lastIt != _lastPullTime.end()) ? lastIt->second : 0;

    return (now - lastTime) >= interval;
}

void RealTimeFrameCache::updateLastPullTime(const std::string& cameraId) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    std::lock_guard<std::mutex> lock(mutex_);
    _lastPullTime[cameraId] = now;
}
