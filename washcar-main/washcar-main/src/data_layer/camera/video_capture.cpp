#include "data_layer/camera/video_capture.h"
#include "common/log/log_manager.h"
#include <iostream>

VideoCapture::VideoCapture(const std::string& rtsp_url)
    : rtspUrl_(rtsp_url) {
    avformat_network_init();
}

VideoCapture::~VideoCapture() {
    closeStream();
    avformat_network_deinit();
}

bool VideoCapture::openStream() {
    pFormatCtx_ = avformat_alloc_context();
    if (!pFormatCtx_) {
        LOG_ERROR("Failed to allocate AVFormatContext");
        return false;
    }

    LOG_INFO("Opening RTSP stream: " + rtspUrl_);

    AVDictionary* options = NULL;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "max_delay", "500000", 0);
    av_dict_set(&options, "stimeout", "5000000", 0);

    int ret = avformat_open_input(&pFormatCtx_, rtspUrl_.c_str(), NULL, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOG_ERROR("Failed to open RTSP stream: " + std::string(err_buf));
        avformat_free_context(pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    ret = avformat_find_stream_info(pFormatCtx_, NULL);
    if (ret < 0) {
        LOG_ERROR("Failed to find stream info");
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    videoStreamIndex_ = -1;
    for (unsigned int i = 0; i < pFormatCtx_->nb_streams; i++) {
        if (pFormatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = i;
            break;
        }
    }

    if (videoStreamIndex_ < 0) {
        LOG_ERROR("No video stream found");
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    AVCodecParameters* codecpar = pFormatCtx_->streams[videoStreamIndex_]->codecpar;
    pCodec_ = avcodec_find_decoder(codecpar->codec_id);
    if (!pCodec_) {
        LOG_ERROR("Codec not found");
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    pCodecCtx_ = avcodec_alloc_context3(pCodec_);
    if (!pCodecCtx_) {
        LOG_ERROR("Failed to allocate codec context");
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    avcodec_parameters_to_context(pCodecCtx_, codecpar);

    ret = avcodec_open2(pCodecCtx_, pCodec_, NULL);
    if (ret < 0) {
        LOG_ERROR("Failed to open codec");
        avcodec_free_context(&pCodecCtx_);
        pCodecCtx_ = nullptr;
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    width_ = codecpar->width;
    height_ = codecpar->height;

    swsCtx_ = sws_getContext(
        width_, height_, pCodecCtx_->pix_fmt,
        width_, height_, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL
    );

    if (!swsCtx_) {
        LOG_ERROR("Failed to create SwsContext");
        avcodec_free_context(&pCodecCtx_);
        pCodecCtx_ = nullptr;
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    int rgbSize = width_ * height_ * 3;
    frameBuffer_ = (uint8_t*)av_malloc(rgbSize);
    if (!frameBuffer_) {
        LOG_ERROR("Failed to allocate frame buffer");
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
        avcodec_free_context(&pCodecCtx_);
        pCodecCtx_ = nullptr;
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
        return false;
    }

    LOG_INFO("RTSP stream opened successfully: " + std::to_string(width_) + "x" + std::to_string(height_));
    return true;
}

bool VideoCapture::readPacket(AVPacket* packet) {
    if (!pFormatCtx_) return false;

    int ret = av_read_frame(pFormatCtx_, packet);
    if (ret < 0) {
        // readPacket 失败可能是网络抖动（av_read_frame 返回 AVERROR(EAGAIN)）
        // 或流真正断了（AVERROR_EOF / 其他负值）。静默返回 false，
        // 调用方 (camera.cpp) 会有自己的 DEBUG 提示"重试"，避免双层日志刷屏。
        // 真正异常需要排查时再升级这里。
        return false;
    }

    if (packet->stream_index != videoStreamIndex_) {
        av_packet_unref(packet);
        return false;
    }

    return true;
}

bool VideoCapture::decodeFrame(const AVPacket* packet, uint8_t*& outData, int& outWidth, int& outHeight) {
    if (!pCodecCtx_ || !packet) return false;

    int ret = avcodec_send_packet(pCodecCtx_, packet);
    if (ret < 0) {
        return false;
    }

    AVFrame* decodedFrame = av_frame_alloc();
    if (!decodedFrame) {
        return false;
    }

    ret = avcodec_receive_frame(pCodecCtx_, decodedFrame);
    if (ret < 0) {
        av_frame_free(&decodedFrame);
        return false;
    }

    uint8_t* dstData[4] = {frameBuffer_, NULL, NULL, NULL};
    int dstLinesize[4] = {width_ * 3, 0, 0, 0};

    sws_scale(swsCtx_, decodedFrame->data, decodedFrame->linesize, 0, height_, dstData, dstLinesize);

    outData = (uint8_t*)av_malloc(width_ * height_ * 3);
    if (outData) {
        memcpy(outData, frameBuffer_, width_ * height_ * 3);
    }

    outWidth = width_;
    outHeight = height_;

    av_frame_free(&decodedFrame);
    return outData != nullptr;
}

void VideoCapture::closeStream() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (frameBuffer_) {
        av_free(frameBuffer_);
        frameBuffer_ = nullptr;
    }

    if (pCodecCtx_) {
        avcodec_free_context(&pCodecCtx_);
        pCodecCtx_ = nullptr;
    }

    if (pFormatCtx_) {
        avformat_close_input(&pFormatCtx_);
        pFormatCtx_ = nullptr;
    }
}

AVPacket* VideoCapture::allocatePacket() {
    return av_packet_alloc();
}

void VideoCapture::freePacket(AVPacket* packet) {
    if (packet) {
        av_packet_free(&packet);
    }
}
