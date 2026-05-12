#include "business_layer/device/real_time_frame_cache.h"

RealTimeFrameCache::RealTimeFrameCache() {

}

void RealTimeFrameCache::updateCameraRealTimeFrame() {

}

CameraRealTimeFrame RealTimeFrameCache::getCameraRealTimeFrame( const CameraInfo& info) {
    auto it = _cameraFrameMap.find(info.getDeviceId());
    if(it == _cameraFrameMap.end() )
        return CameraRealTimeFrame();

    return it -> second;
}