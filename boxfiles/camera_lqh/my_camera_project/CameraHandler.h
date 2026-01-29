#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/avutil.h>
}

class CameraHandler {
public:
    // 构造函数：传入RTSP地址、JPG保存路径、抓拍间隔
    CameraHandler(const std::string& rtspUrl, const std::string& jpgPath, int interval = 5);
    ~CameraHandler();

    bool start();
    void stop();

private:
    void processLoop();
    bool initInput();
    bool initOutput(int index);
    void closeOutput();
    void saveJpg(AVFrame* frame);

    std::string rtspUrl_;
    std::string jpgPath_;
    int interval_;
    std::atomic<bool> running_{false};
    std::thread workThread_;

    AVFormatContext* ifmtCtx_ = nullptr;
    AVFormatContext* ofmtCtx_ = nullptr;
    AVCodecContext* decCtx_ = nullptr;
    int videoIdx_ = -1;
    
    int fileCount_ = 0;
    int64_t frameCount_ = 0;
    double fps_ = 25.0;
    time_t lastJpgTime_ = 0;
};