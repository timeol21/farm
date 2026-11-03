#ifndef CAMERA_H
#define CAMERA_H

#include "data_layer/camera/camera_status.h"
#include "data_layer/camera/camera_history_video.h"
#include "data_layer/camera/camera_device.h"
#include "data_layer/camera/video_capture.h"
#include "data_layer/camera/camera_real_time_frame.h"

extern "C" {
#include <libavutil/time.h>
}

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

class RealTimeFrameCache;

class Camera {
public:
    Camera();
    explicit Camera(const CameraDevice& device);
    ~Camera();

    bool start();
    bool stop();
    bool isRunning() const;
    
    bool getLastKeyFrame(FrameData& out);
    CameraRealTimeFrame getRealTimeFrame();

    bool startRecording(const std::string& outputDir);
    bool stopRecording();
    bool isRecording() const;
    CameraHistoryVideo getHistoryVideo();

    CameraStatus getStatus();

    const std::string& getCameraId() const { return cameraId_; }
    const std::string& getRtspUrl() const { return rtspUrl_; }

    void setFrameCache(RealTimeFrameCache* frameCache);

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
    RealTimeFrameCache* frameCache_ = nullptr;
};

#endif