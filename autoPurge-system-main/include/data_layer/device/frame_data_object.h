#pragma once
#include <memory>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/time.h>
}

struct CameraChannelInfo{
    std::string deviceId;  //这个对于也是
    std::string relevanceId;
};

struct FrameData { 
    std::shared_ptr<AVFrame> frame; 
    int width;
    int height;
    bool valid = false; 
    uint64_t lastKeyFrameTime ; 
};

struct CameraFrameSnapshot { 
    CameraChannelInfo cameraInfo; // 摄像头通道信息
    std::shared_ptr<FrameData> frame;  // 当前最新帧
    uint64_t timestamp = 0; // 更新时间 
};







