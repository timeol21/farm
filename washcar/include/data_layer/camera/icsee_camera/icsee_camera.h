#pragma once

#include "data_layer/camera/camera.h"

#include <string>
#include <thread>
#include <mutex>
#include <memory>

extern "C"
{

#include <libavformat/avformat.h>

#include <libavcodec/avcodec.h>

#include <libswscale/swscale.h>

#include <libavutil/time.h>

}

class RtspClient;

class IcSeeCamera : public Camera
{

public:

    IcSeeCamera(
        const std::string& rtspUrl,
        const std::string& cameraId,
        int keyFrameIntervalSec = 5
    );

    ~IcSeeCamera() override;


    bool initialize() override;

    bool start() override;

    void stop() override;

    bool getLatestKeyFrame(AVFrame*& frame);

private:

    void captureLoop();

    bool openStream();

    void closeStream();

    void processFrame(AVFrame* frame);

    bool saveFrameAsJpeg(AVFrame* frame,const std::string& path);

    std::unique_ptr<RtspClient> rtspClient_;
    
    AVCodecContext* codecContext_ = nullptr;

    AVFrame* frame_ = nullptr;

    AVFrame* latestKeyFrame_ = nullptr;

    std::string cameraId_;

    std::string rtspUrl_;

    std::thread captureThread_;

    bool running_ = false;

    AVFormatContext* formatContext_ = nullptr;

    int videoStreamIndex_ = -1;

    std::mutex frameMutex_;

    int keyFrameIntervalSec_ = 5;

    int64_t lastSaveTime_ = 0;



};

