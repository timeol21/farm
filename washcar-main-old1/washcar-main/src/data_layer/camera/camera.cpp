#include "data_layer/camera/camera.h"
#include "business_layer/device/real_time_frame_cache.h"
#include "common/log/log_manager.h"
#include <chrono>

extern "C" {
#include <libavutil/time.h>
}

Camera::Camera(const CameraDevice& device)
    : device_(device),
      cameraId_(device.getCameraId()),
      name_(device.getName()),
      rtspUrl_(device.getRtspUrl()),
      onlineStatus_(CameraOnlineStatus::OFFLINE),
      videoCapture_(rtspUrl_),
      historyVideo_(std::make_unique<CameraHistoryVideo>()) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    onlineStatus_ = CameraOnlineStatus::OFFLINE;
    start();
}

Camera::~Camera() {
    stop();
}

bool Camera::start() {
    if (isRunning_.load()) return true;

    if (pullThread_.joinable()) {
        pullThread_.join();
    }

    isRunning_ = true;
    pullThread_ = std::thread(&Camera::pullKeyFrameLoop, this);
    return true;
}

bool Camera::stop() {
    isRunning_ = false;
    if (pullThread_.joinable()) {
        pullThread_.join();
    }

    videoCapture_.closeStream();

    std::lock_guard<std::mutex> lock(statusMutex_);
    onlineStatus_ = CameraOnlineStatus::OFFLINE;
    return true;
}

bool Camera::isRunning() const {
    return isRunning_.load();
}

bool Camera::getLastKeyFrame(FrameData& out) {
    std::lock_guard<std::mutex> lock(keyFrameMutex_);
    if (!lastKeyFrame_.data) {
        return false;
    }
    out = lastKeyFrame_;
    return true;
}

CameraRealTimeFrame Camera::getRealTimeFrame() {
    FrameData frame;
    if (getLastKeyFrame(frame)) {
        return CameraRealTimeFrame(frame);
    }
    return CameraRealTimeFrame();
}

bool Camera::startRecording(const std::string& outputDir) {
    std::lock_guard<std::mutex> lock(recordingMutex_);

    if (!isRunning_.load()) {
        LOG_ERROR("Camera: Cannot start recording, camera is not running");
        return false;
    }

    if (historyVideo_->isRecording()) {
        LOG_WARNING("Camera: Already recording");
        return false;
    }

    time_t now = time(nullptr);
    char filename[256];
    strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", localtime(&now));
    std::string fullPath = outputDir + "/camera_" + cameraId_ + "_" + filename + ".mp4";

    bool success = historyVideo_->startRecording(fullPath, videoCapture_.getFormatContext());
    if (success) {
        LOG_INFO("Camera: Started recording to " + fullPath);
    }
    return success;
}

bool Camera::stopRecording() {
    std::lock_guard<std::mutex> lock(recordingMutex_);

    bool success = historyVideo_->stopRecording();
    if (success) {
        LOG_INFO("Camera: Stopped recording");
    }
    return success;
}

bool Camera::isRecording() const {
    return historyVideo_->isRecording();
}

CameraHistoryVideo Camera::getHistoryVideo() {
    return *historyVideo_;
}

CameraStatus Camera::getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return CameraStatus(cameraId_, 2, name_, onlineStatus_);
}

void Camera::pullKeyFrameLoop() {
    LOG_INFO("=== Camera Pull KeyFrame Loop Started ===");
    LOG_INFO("Camera ID: " + cameraId_ + ", RTSP URL: " + rtspUrl_);

    if (!videoCapture_.openStream()) {
        LOG_ERROR("Failed to open RTSP stream for camera: " + cameraId_);
        isRunning_ = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        onlineStatus_ = CameraOnlineStatus::ONLINE;
    }

    AVPacket* packet = videoCapture_.allocatePacket();
    if (!packet) {
        LOG_ERROR("Failed to allocate packet");
        videoCapture_.closeStream();
        isRunning_ = false;
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            onlineStatus_ = CameraOnlineStatus::OFFLINE;
        }
        return;
    }

    int totalPackets = 0;
    while (isRunning_.load()) {
        if (!videoCapture_.readPacket(packet)) {
            // RTSP 拉流偶发失败很常见，循环重试即可。降级到 DEBUG，
            // 避免现场终端被一秒 20 条的 WARNING 刷屏。
            //LOG_DEBUG("Camera failed to read packet, retrying...");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        uint8_t* rgbData = nullptr;
        int width = 0;
        int height = 0;

        if (videoCapture_.decodeFrame(packet, rgbData, width, height)) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                std::lock_guard<std::mutex> lock(keyFrameMutex_);
                lastKeyFrame_.data.reset(rgbData, [](uint8_t* p) { av_free(p); });
                lastKeyFrame_.width = width;
                lastKeyFrame_.height = height;
                // [FIX-04] 帧时间戳统一为 system_clock 毫秒(epoch):
                //   原为 av_gettime_relative()(相对时间·微秒), 而 DetectionResult::isValid()
                //   用 system_clock(毫秒) 相减, 基准+单位全错 → isValid 恒 false,
                //   “3 秒有效期”形同虚设。
                lastKeyFrame_.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                if (frameCache_) {
                    frameCache_->updateCameraRealTimeFrame(cameraId_, lastKeyFrame_);
                }
            } else {
                av_free(rgbData);
            }
        }

        if (historyVideo_ && historyVideo_->isRecording()) {
            historyVideo_->writePacket(packet);
        }

        av_packet_unref(packet);
        totalPackets++;

        if (totalPackets % 200 == 0) {
            LOG_INFO("Camera " + cameraId_ + ": Processed " + std::to_string(totalPackets) + " packets");
        }
    }

    videoCapture_.freePacket(packet);
    videoCapture_.closeStream();

    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        onlineStatus_ = CameraOnlineStatus::OFFLINE;
    }

    LOG_INFO("=== Camera " + cameraId_ + " Pull KeyFrame Loop Finished ===");
}

void Camera::setFrameCache(RealTimeFrameCache* frameCache) {
    frameCache_ = frameCache;
}
