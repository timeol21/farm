#pragma once

#include "data_layer/camera/camera.h"
#include "userinterface_layer/rtsp/rtsp_client.h"
#include "data_layer/camera/icsee_camera/rtsp_mp4_recorder.h"
#include "common/config/device_config.h"
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

class RtspMp4Recorder;

class IcSeeCamera : public Camera
{

public:

    explicit IcSeeCamera(
        const DeviceConfig& config
    );

    ~IcSeeCamera() override;


    bool initialize() override;

    bool start() override;

    void stop() override;

    bool getLatestKeyFrame(AVFrame*& frame);

private:

    bool buildRtspUrl();

    void captureLoop();

    bool openStream();

    void closeStream();

    void processFrame(AVFrame* frame);

    bool saveFrameAsJpeg(AVFrame* frame,const std::string& path);
    
    bool startRecord();

    void stopRecord();

    bool writePacket(AVPacket* packet);

    std::unique_ptr<RtspClient> rtspClient_;
    
    AVCodecContext* codecContext_ = nullptr;
    
    AVFormatContext* outputContext_;

    AVFrame* frame_ = nullptr;

    AVFrame* latestKeyFrame_ = nullptr;

    std::string cameraId_;

    std::string rtspUrl_;

    std::thread captureThread_;

    std::string recordPath_;

    bool running_ = false;
    
    bool recordEnable_;

    std::mutex frameMutex_;

    int keyFrameIntervalSec_;
    
    int recordSegmentTime_;

    int64_t lastSaveTime_ = 0;

    std::unique_ptr<RtspMp4Recorder> recorder_;


};

