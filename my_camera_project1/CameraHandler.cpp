#include "CameraHandler.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>

extern "C" {
#include <libavutil/opt.h>
}

CameraHandler::CameraHandler(const std::string& rtspUrl, const std::string& name, int jpgInterval)
    : rtspUrl_(rtspUrl), name_(name), jpgInterval_(jpgInterval) {
    avformat_network_init();
}

CameraHandler::~CameraHandler() {
    stop();
}

void CameraHandler::start() {
    running_ = true;
    // 启动录制线程：不解码，重封装，低CPU占用
    recordThread_ = std::thread(&CameraHandler::recordLoop, this);
    // 启动抓拍线程：解码，每5秒生成一张图片
    snapThread_ = std::thread(&CameraHandler::snapLoop, this);
}

void CameraHandler::stop() {
    running_ = false;
    if (recordThread_.joinable()) recordThread_.join();
    if (snapThread_.joinable()) snapThread_.join();
}

// ===================== 录制逻辑 (Thread 1) =====================
void CameraHandler::recordLoop() {
    AVFormatContext *ifmt = nullptr, *ofmt = nullptr;
    AVPacket* pkt = av_packet_alloc();
    int video_idx = -1;
    int segment_idx = 0;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0); // 强制使用 TCP
    av_dict_set(&opts, "stimeout", "5000000", 0);

    if (avformat_open_input(&ifmt, rtspUrl_.c_str(), nullptr, &opts) < 0) return;
    if (avformat_find_stream_info(ifmt, nullptr) < 0) return;

    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }
    if (video_idx == -1) return;

    auto open_new_mp4 = [&]() {
        if (ofmt) {
            av_write_trailer(ofmt);
            avio_closep(&ofmt->pb);
            avformat_free_context(ofmt);
            ofmt = nullptr;
        }
        std::string filename = name_ + "_rec_" + std::to_string(segment_idx++) + ".mp4";
        avformat_alloc_output_context2(&ofmt, nullptr, nullptr, filename.c_str());
        AVStream* out_s = avformat_new_stream(ofmt, nullptr);
        avcodec_parameters_copy(out_s->codecpar, ifmt->streams[video_idx]->codecpar);
        out_s->codecpar->codec_tag = 0;

        if (avio_open(&ofmt->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;

        AVDictionary* out_opts = nullptr;
        // 核心：支持 VS Code 边录边看
        av_dict_set(&out_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        avformat_write_header(ofmt, &out_opts);
        av_dict_free(&out_opts);
        std::cout << "[" << name_ << "] 开始新片段: " << filename << std::endl;
    };

    open_new_mp4();
    int64_t start_pts = -1;
    int64_t last_dts = -1;

    while (running_ && av_read_frame(ifmt, pkt) >= 0) {
        if (pkt->stream_index == video_idx) {
            if (start_pts == -1) start_pts = pkt->pts;

            double elapsed = (pkt->pts - start_pts) * av_q2d(ifmt->streams[video_idx]->time_base);
            if (elapsed >= 60 && (pkt->flags & AV_PKT_FLAG_KEY)) {
                open_new_mp4();
                start_pts = pkt->pts;
                last_dts = -1;
            }

            // 时间戳修正逻辑
            AVStream *in_s = ifmt->streams[video_idx], *out_s = ofmt->streams[0];
            pkt->pts = av_rescale_q_rnd(pkt->pts, in_s->time_base, out_s->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->dts = av_rescale_q_rnd(pkt->dts, in_s->time_base, out_s->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt->duration = av_rescale_q(pkt->duration, in_s->time_base, out_s->time_base);
            pkt->pos = -1;

            if (last_dts != -1 && pkt->dts <= last_dts) pkt->dts = last_dts + 1;
            if (pkt->pts < pkt->dts) pkt->pts = pkt->dts;
            last_dts = pkt->dts;

            pkt->stream_index = 0;
            av_interleaved_write_frame(ofmt, pkt);
            avio_flush(ofmt->pb);
        }
        av_packet_unref(pkt);
    }

    if (ofmt) { av_write_trailer(ofmt); avio_closep(&ofmt->pb); avformat_free_context(ofmt); }
    avformat_close_input(&ifmt);
    av_packet_free(&pkt);
}

// ===================== 抓拍逻辑 (Thread 2) =====================
void CameraHandler::snapLoop() {
    AVFormatContext* ifmt = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int64_t last_snap = 0;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt, rtspUrl_.c_str(), nullptr, &opts) < 0) return;
    avformat_find_stream_info(ifmt, nullptr);

    int v_idx = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* dec = avcodec_find_decoder(ifmt->streams[v_idx]->codecpar->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, ifmt->streams[v_idx]->codecpar);
    avcodec_open2(dec_ctx, dec, nullptr);

    while (running_ && av_read_frame(ifmt, pkt) >= 0) {
        if (pkt->stream_index == v_idx) {
            int64_t now = av_gettime() / 1000000;
            if (now - last_snap >= jpgInterval_ && (pkt->flags & AV_PKT_FLAG_KEY)) {
                if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                    if (avcodec_receive_frame(dec_ctx, frame) == 0) {
                        std::string path = name_ + "_latest.jpg";
                        std::string tmp = path + ".tmp";
                        if (saveFrameAsJpeg(frame, tmp)) {
                            rename(tmp.c_str(), path.c_str()); // 原子替换
                            last_snap = now;
                            std::cout << "[" << name_ << "] 更新图片: " << path << std::endl;
                        }
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }
    avcodec_free_context(&dec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&ifmt);
}

// ===================== 图片编码辅助函数 =====================
bool CameraHandler::saveFrameAsJpeg(AVFrame* frame, const std::string& path) {
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* ctx = avcodec_alloc_context3(enc);
    ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    ctx->width = frame->width;
    ctx->height = frame->height;
    ctx->time_base = {1, 25};

    if (avcodec_open2(ctx, enc, nullptr) < 0) { avcodec_free_context(&ctx); return false; }

    AVPacket* pkt = av_packet_alloc();
    bool success = false;
    if (avcodec_send_frame(ctx, frame) == 0 && avcodec_receive_packet(ctx, pkt) == 0) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { fwrite(pkt->data, 1, pkt->size, f); fclose(f); success = true; }
    }
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return success;
}