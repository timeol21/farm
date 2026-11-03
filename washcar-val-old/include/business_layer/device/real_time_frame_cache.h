#pragma once

#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "data_layer/camera/camera_real_time_frame.h"
#include "data_layer/camera/camera_info.h"
#include "data_layer/camera/camera_utils.h"
#include "data_layer/camera/frame_sink.h"

// 实时帧缓存的唯一实现，也是 Camera 的帧接收方（IFrameSink）。
// 依赖方向：business_layer -> data_layer/camera/frame_sink.h（接口），不再反向。
class RealTimeFrameCache : public IFrameSink {
public:
    RealTimeFrameCache();
    ~RealTimeFrameCache() = default;

    // IFrameSink：Camera 拉流线程的回调入口，实现里直接转调 updateCameraRealTimeFrame
    void onFrame(const std::string& cameraId, const FrameData& frame) override;

    void updateCameraRealTimeFrame(const std::string& cameraId, const FrameData& frameData);
    CameraRealTimeFrame getCameraRealTimeFrame(const CameraInfo& info);

    // [预留未启用] 以下五个方法当前无外部调用者。Camera::pullKeyFrameLoop 固定按
    // VideoCapture 的节奏写帧，不经过这里的频率控制。
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
