#include "logger.h"          // 引入自定义日志头文件
#include "video_data_object.h"
#include <mutex>             // std::lock_guard/std::mutex



// ========== 1. 获取摄像头状态 ==========
CameraStatus Camera::getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex_);
    // 补充INFO日志（若频繁调用可改为DEBUG级别）
    // LOG_INFO("[Camera::getStatus] 获取摄像头状态，通道号：" + std::to_string(info_.channel) + 
    //          "，当前状态：" + std::to_string(static_cast<int>(status_)) + "（" + cameraStatusToString(status_) + "）");
    return status_;
}

// 辅助函数：将CameraStatus转为字符串（便于日志展示）
std::string Camera::cameraStatusToString(CameraStatus status) {
    switch (status) {
        case CameraStatus::RUNNING: return "RUNNING";
        case CameraStatus::OFFLINE: return "OFFLINE";
        default: return "UNKNOWN";
    }
}

// ========== 2. 获取最新关键帧 ==========
bool Camera::getLastKeyFrame(FrameData& out) {
    LOG_INFO("[Camera::getLastKeyFrame] 尝试获取最新关键帧，通道号：" + std::to_string(info_.channel));

    if (!lastKeyFrame_.frame || lastKeyFrame_.width == 0 || lastKeyFrame_.height == 0) {
        LOG_WARNING("[Camera::getLastKeyFrame] （帧指针为空/宽高为0），通道号：" + std::to_string(info_.channel));
        return false;
    }

    AVFrame* clone = av_frame_clone(lastKeyFrame_.frame.get());
    if (!clone) {
        LOG_ERROR("[Camera::getLastKeyFrame] 克隆关键帧失败，通道号：" + std::to_string(info_.channel));
        return false;
    }

    out.frame = std::shared_ptr<AVFrame>(clone, [](AVFrame* f){ av_frame_free(&f); });
    out.width = lastKeyFrame_.width;
    out.height = lastKeyFrame_.height;
    out.lastKeyFrameTime = lastKeyFrame_.lastKeyFrameTime;
    
    // LOG_INFO("[Camera::getLastKeyFrame] 获取关键帧成功，通道号：" + std::to_string(info_.channel) + 
    //          "，帧宽：" + std::to_string(out.width) + "，帧高：" + std::to_string(out.height) + 
    //          "，关键帧时间戳：" + std::to_string(out.lastKeyFrameTime));
    return true;
}

// ========== 3. 处理编码帧（核心解码函数） ==========
void Camera::onEncodedFrame(uint8_t* data, size_t len) {
    if (!data || len == 0 || !codecCtx_) {
        updateStatus(CameraStatus::OFFLINE);
        return;
    }
  
    if (!decoderInitialized_) {
        AVCodecID id = detectCodec(data, len);
        if (id == AV_CODEC_ID_NONE) {
            std::cerr << "[Camera] 未检测到有效编码类型" << std::endl;
            return;
        }

        const AVCodec* codec = avcodec_find_decoder(id);
        codecCtx_->codec_id = id;
        codecCtx_->codec_type = AVMEDIA_TYPE_VIDEO;

        if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
            return;
        }
        decoderInitialized_ = true;
    }
  
    // 分配并初始化AVPacket
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return;
    }

    pkt->data = data;
    pkt->size = static_cast<int>(len);

    // 发送数据包到解码器（加锁避免多线程冲突）
    int ret = 0;
    {
        std::lock_guard<std::mutex> lock(decoderMutex_);
        ret = avcodec_send_packet(codecCtx_, pkt);
    }

    if (ret < 0) {
        logFFmpegError("avcodec_send_packet", ret);
        av_packet_free(&pkt);
        updateStatus(CameraStatus::OFFLINE);
        return;
    }

    // 接收解码后的帧
    {
        std::lock_guard<std::mutex> lock(decoderMutex_);
        ret = avcodec_receive_frame(codecCtx_, frameYUV_);
    }

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_packet_free(&pkt);
        return;
    } else if (ret < 0) {
        logFFmpegError("avcodec_receive_frame", ret);
        av_packet_free(&pkt);
        updateStatus(CameraStatus::OFFLINE);
        return;
    }
    
    // 只处理关键帧（保留YUV原始帧，删除RGB转换）
    if (frameYUV_->key_frame == 1) {
        // 验证YUV帧数据有效性
        if (!frameYUV_->data[0] || !frameYUV_->data[1] || !frameYUV_->data[2]) {
            std::cerr << "[Camera] 通道" << info_.channel << " YUV帧指针为空" << std::endl;
            av_packet_free(&pkt);
            return;
        }

        // 直接保存YUV关键帧（不再转换为RGB）
        AVFrame* keyFrameCopy = av_frame_clone(frameYUV_);
        if (!keyFrameCopy) {
            std::cerr << "Failed to clone frameYUV_ for lastKeyFrame_" << std::endl;
        } else {
            std::lock_guard<std::mutex> lock(frameMutex_);  // 改用frameMutex_（删除了frameRGBMutex_）
            lastKeyFrame_.frame = std::shared_ptr<AVFrame>(keyFrameCopy, [](AVFrame* f){
                av_frame_free(&f);
            });
            lastKeyFrame_.width = frameYUV_->width;
            lastKeyFrame_.height = frameYUV_->height;
            lastKeyFrame_.lastKeyFrameTime = av_gettime_relative();
        }

        av_packet_free(&pkt);
        // 重置YUV帧（准备接收下一帧）
        av_frame_unref(frameYUV_);
    }
}

// ========== 4. 打印FFmpeg错误信息（封装为日志） ==========
void Camera::logFFmpegError(const char* func, int err) {
    char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, err_buf, sizeof(err_buf));
    LOG_ERROR("[Camera::logFFmpegError] FFmpeg错误：函数=" + std::string(func) + 
              "，错误信息=" + std::string(err_buf) + "，错误码=" + std::to_string(err) + 
              "，通道号：" + std::to_string(info_.channel));
}

// ========== 5. 初始化解码器 ==========
bool Camera::initDecoder() {
    LOG_INFO("[Camera::initDecoder] 开始初始化解码器，通道号：" + std::to_string(info_.channel));

    // 分配解码器上下文
    codecCtx_ = avcodec_alloc_context3(nullptr);
    if (!codecCtx_) {
        LOG_ERROR("[Camera::initDecoder] 分配解码器上下文失败，通道号：" + std::to_string(info_.channel));
        return false;
    }

    // 开启自动检测编码格式
    codecCtx_->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    LOG_INFO("[Camera::initDecoder] 解码器上下文初始化成功，已开启自动检测编码格式，通道号：" + std::to_string(info_.channel));

    // 初始化YUV帧（补充：原代码未初始化frameYUV_，建议添加）
    frameYUV_ = av_frame_alloc();
    if (!frameYUV_) {
        LOG_ERROR("[Camera::initDecoder] 分配YUV帧失败，通道号：" + std::to_string(info_.channel));
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
        return false;
    }

    decoderInitialized_ = false; // 初始化为未初始化状态
    return true;
}

// ========== 6. 检测编码类型 ==========
AVCodecID Camera::detectCodec(uint8_t* data, size_t len) {
    LOG_INFO("[Camera::detectCodec] 检测编码类型，通道号：" + std::to_string(info_.channel) + 
             "，数据长度：" + std::to_string(len) + "字节");

    if (len < 5) {
        LOG_WARNING("[Camera::detectCodec] 数据长度不足（<5字节），无法检测编码类型，通道号：" + std::to_string(info_.channel));
        return AV_CODEC_ID_NONE;
    }
    // AnnexB 00 00 00 01
    if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
        uint8_t nal = data[4];
        LOG_INFO("[Camera::detectCodec] 检测到AnnexB格式NALU头，NAL类型：0x" + std::to_string(nal) + 
                 "，通道号：" + std::to_string(info_.channel));

        if ((nal & 0x1F) == 7) {
            LOG_INFO("[Camera::detectCodec] 检测到H264 SPS，通道号：" + std::to_string(info_.channel));
            return AV_CODEC_ID_H264; // H264 SPS
        }
        if ((nal >> 1) == 0x20) {
            LOG_INFO("[Camera::detectCodec] 检测到H265 VPS，通道号：" + std::to_string(info_.channel));
            return AV_CODEC_ID_HEVC; // H265 VPS
        }
    }

    LOG_WARNING("[Camera::detectCodec] 未识别的编码类型（非H264/H265），通道号：" + std::to_string(info_.channel));
    return AV_CODEC_ID_NONE;
}

// ========== 7. 更新最新关键帧（带锁） ==========
void Camera::updateKeyFrame(FrameData newKeyFrame) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    lastKeyFrame_ = newKeyFrame;
    // LOG_INFO("[Camera::updateKeyFrame] 更新最新关键帧，通道号：" + std::to_string(info_.channel) + 
    //          "，帧宽：" + std::to_string(newKeyFrame.width) + "，帧高：" + std::to_string(newKeyFrame.height) + 
    //          "，时间戳：" + std::to_string(newKeyFrame.lastKeyFrameTime));
}

// ========== 8. 更新状态（带锁） ==========
void Camera::updateStatus(CameraStatus status) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    CameraStatus oldStatus = status_;
    status_ = status;
    // LOG_INFO("[Camera::updateStatus] 摄像头状态更新，通道号：" + std::to_string(info_.channel) + 
    //          "，旧状态：" + cameraStatusToString(oldStatus) + "，新状态：" + cameraStatusToString(status));
}