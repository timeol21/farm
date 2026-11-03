#pragma once

#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "data_layer/camera/camera_real_time_frame.h"
#include "data_layer/camera/camera_info.h"
#include "data_layer/camera/camera_utils.h"

class RealTimeFrameCache {
public:
    RealTimeFrameCache();
    ~RealTimeFrameCache() = default;

    void updateCameraRealTimeFrame(const std::string& cameraId, const FrameData& frameData);
    CameraRealTimeFrame getCameraRealTimeFrame(const CameraInfo& info);

    // 频率控制
    void setFramePullInterval(const std::string& cameraId, int intervalMs);
    void setIdleMode(const std::string& cameraId);   // 空闲模式：10秒
    void setWorkingMode(const std::string& cameraId); // 工作模式：5秒
    bool shouldPullFrame(const std::string& cameraId);
    void updateLastPullTime(const std::string& cameraId);

private:
    std::unordered_map<std::string, CameraRealTimeFrame> _cameraFrameMap;
    std::unordered_map<std::string, int> _framePullIntervals;  // cameraId -> intervalMs
    std::unordered_map<std::string, int64_t> _lastPullTime;  // cameraId -> lastTime
    std::mutex mutex_;
};
