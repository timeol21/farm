#ifndef FRAME_SINK_H
#define FRAME_SINK_H

#include <string>

#include "data_layer/camera/camera_utils.h"   // FrameData

// 帧接收方接口。
// Camera（数据层）只依赖这个抽象，不反向依赖 business_layer 的具体缓存实现。
// 目前唯一的实现是 business_layer/device/real_time_frame_cache.h 的 RealTimeFrameCache。
class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    // 摄像头每解出一帧关键帧就回调一次。
    // 实现方必须自行保证线程安全（Camera 的拉流线程调用它）。
    virtual void onFrame(const std::string& cameraId, const FrameData& frame) = 0;
};

#endif
