#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <memory>

class VideoCapture {
public:
    explicit VideoCapture(const std::string& rtsp_url);
    ~VideoCapture();

    bool openStream();
    bool readPacket(AVPacket* packet);
    bool decodeFrame(const AVPacket* packet, uint8_t*& outData, int& outWidth, int& outHeight);
    void closeStream();

    AVPacket* allocatePacket();
    void freePacket(AVPacket* packet);

    AVFormatContext* getFormatContext() { return pFormatCtx_; }
    int getVideoWidth() const { return width_; }
    int getVideoHeight() const { return height_; }

private:
    std::string rtspUrl_;
    AVFormatContext* pFormatCtx_ = nullptr;
    AVCodecContext* pCodecCtx_ = nullptr;
    const AVCodec* pCodec_ = nullptr;
    int videoStreamIndex_ = -1;
    SwsContext* swsCtx_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    uint8_t* frameBuffer_ = nullptr;
};

#endif
