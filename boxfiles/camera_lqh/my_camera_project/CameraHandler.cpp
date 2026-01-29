#include "CameraHandler.h"
#include <iostream>

CameraHandler::CameraHandler(const std::string& rtspUrl, const std::string& jpgPath, int interval)
    : rtspUrl_(rtspUrl), jpgPath_(jpgPath), interval_(interval) {
    avformat_network_init();
}

CameraHandler::~CameraHandler() {
    stop();
}

bool CameraHandler::start() {
    running_ = true;
    workThread_ = std::thread(&CameraHandler::processLoop, this);
    return true;
}

void CameraHandler::stop() {
    running_ = false;
    if (workThread_.joinable()) workThread_.join();
    closeOutput();
    if (decCtx_) avcodec_free_context(&decCtx_);
    if (ifmtCtx_) avformat_close_input(&ifmtCtx_);
}

bool CameraHandler::initInput() {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // 强制 TCP
    if (avformat_open_input(&ifmtCtx_, rtspUrl_.c_str(), nullptr, &opts) < 0) return false;
    if (avformat_find_stream_info(ifmtCtx_, nullptr) < 0) return false;

    for (unsigned int i = 0; i < ifmtCtx_->nb_streams; i++) {
        if (ifmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx_ = i;
            break;
        }
    }
    if (videoIdx_ == -1) return false;

    AVCodecParameters* params = ifmtCtx_->streams[videoIdx_]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
    decCtx_ = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decCtx_, params);
    if (avcodec_open2(decCtx_, decoder, nullptr) < 0) return false;

    fps_ = av_q2d(ifmtCtx_->streams[videoIdx_]->avg_frame_rate);
    if (fps_ <= 0) fps_ = 25.0;
    return true;
}

bool CameraHandler::initOutput(int index) {
    closeOutput();
    // 为不同摄像头生成带编号的文件名
    std::string filename = "record_" + std::to_string(index) + "_" + std::to_string(time(nullptr)) + ".mp4";
    avformat_alloc_output_context2(&ofmtCtx_, nullptr, nullptr, filename.c_str());
    
    AVStream* outStream = avformat_new_stream(ofmtCtx_, nullptr);
    avcodec_parameters_copy(outStream->codecpar, ifmtCtx_->streams[videoIdx_]->codecpar);
    outStream->codecpar->codec_tag = 0;

    if (avio_open(&ofmtCtx_->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return false;

    AVDictionary* opts = nullptr;
    // 关键：为了 VS Code 边录边看设置的 fMP4 参数
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    int ret = avformat_write_header(ofmtCtx_, &opts);
    av_dict_free(&opts);
    
    frameCount_ = 0;
    return ret >= 0;
}

void CameraHandler::closeOutput() {
    if (ofmtCtx_) {
        av_write_trailer(ofmtCtx_);
        avio_closep(&ofmtCtx_->pb);
        avformat_free_context(ofmtCtx_);
        ofmtCtx_ = nullptr;
    }
}

void CameraHandler::saveJpg(AVFrame* frame) {
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* encCtx = avcodec_alloc_context3(encoder);
    encCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    encCtx->width = frame->width;
    encCtx->height = frame->height;
    encCtx->time_base = {1, 25};

    if (avcodec_open2(encCtx, encoder, nullptr) >= 0) {
        AVPacket* pkt = av_packet_alloc();
        if (avcodec_send_frame(encCtx, frame) == 0 && avcodec_receive_packet(encCtx, pkt) == 0) {
            std::string tmp = jpgPath_ + ".tmp";
            FILE* f = fopen(tmp.c_str(), "wb");
            if (f) {
                fwrite(pkt->data, 1, pkt->size, f);
                fclose(f);
                rename(tmp.c_str(), jpgPath_.c_str());
            }
        }
        av_packet_free(&pkt);
    }
    avcodec_free_context(&encCtx);
}

void CameraHandler::processLoop() {
    if (!initInput()) return;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (running_ && av_read_frame(ifmtCtx_, pkt) >= 0) {
        if (pkt->stream_index == videoIdx_) {
            bool isKey = pkt->flags & AV_PKT_FLAG_KEY;

            if (!ofmtCtx_ && isKey) initOutput(fileCount_++);
            
            if (ofmtCtx_) {
                if (frameCount_ / fps_ >= 60 && isKey) initOutput(fileCount_++);
                
                AVPacket* vPkt = av_packet_clone(pkt);
                AVStream* outS = ofmtCtx_->streams[0];
                vPkt->pts = vPkt->dts = (int64_t)(frameCount_ * (outS->time_base.den / (outS->time_base.num * fps_)));
                vPkt->duration = (int64_t)(outS->time_base.den / (outS->time_base.num * fps_));
                vPkt->stream_index = 0;

                av_interleaved_write_frame(ofmtCtx_, vPkt);
                avio_flush(ofmtCtx_->pb); // 强制刷盘，VS Code 实时观看核心
                av_packet_free(&vPkt);
                frameCount_++;
            }

            time_t now = time(nullptr);
            if (now - lastJpgTime_ >= interval_ && isKey) {
                if (avcodec_send_packet(decCtx_, pkt) == 0) {
                    if (avcodec_receive_frame(decCtx_, frame) == 0) {
                        saveJpg(frame);
                        lastJpgTime_ = now;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
}