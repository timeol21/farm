#ifndef CAMERA_REAL_TIME_FRAME_H
#define CAMERA_REAL_TIME_FRAME_H

#include "data_layer/camera/camera_utils.h"
#include <memory>
#include <string>
#include <vector>

class CameraRealTimeFrame {
public:
    CameraRealTimeFrame() = default;
    explicit CameraRealTimeFrame(const FrameData& frameData);
    ~CameraRealTimeFrame() = default;

    bool saveAsBmp(const std::string& filename);
    std::vector<uint8_t> getBmpData() const;

    // 帧的原始 RGB888 像素,共享帧缓冲的引用(不拷数据)。
    // 帧缓冲本身是 shared_ptr 持有的,这里再存一份只是加引用计数 ——
    // 检测结果要把整帧带出去时,用这个比每轮深拷贝 6MB 省得多。
    std::shared_ptr<const uint8_t> getPixelData() const;

    int getWidth() const;
    int getHeight() const;
    int64_t getTimestamp() const;
    bool isValid() const { return frameData.data != nullptr; }

private:
    FrameData frameData;
};

#endif
