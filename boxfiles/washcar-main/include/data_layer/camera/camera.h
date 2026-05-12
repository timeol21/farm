#ifndef CAMERA_H
#define CAMERA_H

#include "data_layer/camera/camera_status.h"
#include "data_layer/camera/camera_history_video.h"

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
class Camera {
    public:
        Camera() = default;
        Camera(std::string cameraId, std::string name, std::string rtspUrl);
        ~Camera();

        bool start();
        bool stop();
        bool isRunning() const ;
        bool getLastKeyFrame(FrameData& out);

        CameraHistoryVideo getCameraHistoryVideo();

        CameraStatus getStatus();


    private:
        void pullKeyFrameLoop();

        CameraOnlineStatus onlineStatus_ ;
        std::string cameraId_;
        std::string name_;
        std::string rtspUrl_;
        FrameData lastKeyFrame_;
        std::atomic_bool isRunning_{false};
        std::mutex keyFrameMutex_;
        std::mutex sattusMutex_;
};

#endif