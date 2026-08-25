#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H

#include "interfaces/ivideo.h"
#include <atomic>
#include <thread>
#include <memory>

class RK3588VideoCapture : public IVideoSource {
public:
    RK3588VideoCapture(int camera_id = 0, int width = 1920, int height = 1080, int fps = 30);
    ~RK3588VideoCapture();
    
    bool initialize() override;
    bool startCapture() override;
    bool stopCapture() override;
    std::unique_ptr<VideoFrame> getFrame() override;
    bool saveVideo(const std::string& filename) override;
    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }
    int getFps() const override { return fps_; }
    
private:
    void captureLoop();
    
    int camera_id_;
    int width_;
    int height_;
    int fps_;
    std::atomic<bool> capturing_{false};
    std::thread capture_thread_;
    void* v4l2_device_;  // V4L2设备指针
    std::string save_path_;
};

#endif