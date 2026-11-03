#ifndef CAMERA_REAL_TIME_FRAME_H
#define CAMERA_REAL_TIME_FRAME_H

#include "data_layer/camera/camera_utils.h"
#include <string>
#include <vector>

class CameraRealTimeFrame {
public:
    CameraRealTimeFrame() = default;
    explicit CameraRealTimeFrame(const FrameData& frameData);
    ~CameraRealTimeFrame() = default;

    bool saveAsBmp(const std::string& filename);
    std::vector<uint8_t> getBmpData() const;

    int getWidth() const;
    int getHeight() const;
    int64_t getTimestamp() const;
    bool isValid() const { return frameData.data != nullptr; }

private:
    FrameData frameData;
};

#endif
