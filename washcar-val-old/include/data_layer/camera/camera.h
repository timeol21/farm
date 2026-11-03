#ifndef CAMERA_H
#define CAMERA_H

#include "data_layer/camera/camera_status.h"
#include "data_layer/camera/camera_history_video.h"
#include "data_layer/camera/camera_device.h"
#include "data_layer/camera/video_capture.h"
#include "data_layer/camera/camera_real_time_frame.h"
#include "data_layer/camera/frame_sink.h"

extern "C" {
#include <libavutil/time.h>
}

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

class Camera {
public:
    Camera();
    explicit Camera(const CameraDevice& device);
    ~Camera();

    bool start();
    bool stop();
    bool isRunning() const;
    
    // [预留未启用] 只被下面的 getRealTimeFrame() 调用；实时帧统一从 RealTimeFrameCache 取。
    bool getLastKeyFrame(FrameData& out);
    // [预留未启用] 当前无外部调用者。实时帧统一从 RealTimeFrameCache 取，
    // 见 RealTimeFrameCache::getCameraRealTimeFrame。
    CameraRealTimeFrame getRealTimeFrame();

    // [预留未启用] 当前无调用者，历史录像功能未启用。
    // 相关能力（CameraHistoryVideo / VideoCapture）保留，等待后续接入。
    bool startRecording(const std::string& outputDir);
    bool stopRecording();
    bool isRecording() const;
    CameraHistoryVideo getHistoryVideo();

    CameraStatus getStatus();

    const std::string& getCameraId() const { return cameraId_; }
    const std::string& getRtspUrl() const { return rtspUrl_; }

    // 注入帧接收方（推荐：在 start() 之前调用，否则首帧会因 sink 为空被丢弃）
    void setFrameSink(IFrameSink* sink);

private:
    void pullKeyFrameLoop();

    CameraDevice device_;
    std::string cameraId_;
    std::string name_;
    std::string rtspUrl_;
    FrameData lastKeyFrame_;
    std::atomic_bool isRunning_{false};
    std::mutex keyFrameMutex_;
    std::mutex statusMutex_;
    std::mutex recordingMutex_;
    CameraOnlineStatus onlineStatus_;
    
    std::thread pullThread_;
    VideoCapture videoCapture_;
    std::unique_ptr<CameraHistoryVideo> historyVideo_;
    IFrameSink* frameSink_ = nullptr;
};

#endif