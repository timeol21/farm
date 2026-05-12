#ifndef CAMERA_REAL_TIME_FRAME_H
#define CAMERA_REAL_TIME_FRAME_H

#include "data_layer/camera/camera_utils.h"

class CameraRealTimeFrame {
    public:
        CameraRealTimeFrame() = default;
        CameraRealTimeFrame(const FrameData& frameData);
        ~CameraRealTimeFrame() = default;

    private:
        FrameData frameData;

};

#endif