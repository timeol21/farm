#pragma once

#include<unordered_map>
#include "data_layer/camera/camera_real_time_frame.h"
#include "data_layer/camera/camera_info.h"
class RealTimeFrameCache {

    public:

        RealTimeFrameCache() ;
        ~RealTimeFrameCache() = default;
        void updateCameraRealTimeFrame();

        CameraRealTimeFrame getCameraRealTimeFrame( const CameraInfo& info);

    private:
        std::unordered_map<std::string, CameraRealTimeFrame> _cameraFrameMap;
};