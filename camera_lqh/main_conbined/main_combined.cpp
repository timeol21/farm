#include <iostream>
#include <string>
#include <ctime>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

// 编码存图函数
void save_frame_to_jpg(AVFrame* pFrame, const std::string& filename) {
    AVCodec* pEnc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* pEncCtx = avcodec_alloc_context3(pEnc);
    pEncCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; 
    pEncCtx->width = pFrame->width;
    pEncCtx->height = pFrame->height;
    pEncCtx->time_base = {1, 25};

    if (avcodec_open2(pEncCtx, pEnc, nullptr) >= 0) {
        AVPacket* pkt = av_packet_alloc();
        if (avcodec_send_frame(pEncCtx, pFrame) == 0) {
            if (avcodec_receive_packet(pEncCtx, pkt) == 0) {
                FILE* f = fopen(filename.c_str(), "wb");
                if (f) {
                    fwrite(pkt->data, 1, pkt->size, f);
                    fclose(f);
                }
            }
        }
        av_packet_free(&pkt);
    }
    avcodec_free_context(&pEncCtx);
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVCodecContext *pDecCtx = nullptr;
    AVPacket *pPkt = av_packet_alloc();
    AVFrame *pFrame = av_frame_alloc();
    
    int video_idx = -1;
    int file_count = 0;
    const int SEGMENT_DURATION = 60; 
    const int JPG_INTERVAL = 5;
    
    int64_t frame_count_in_seg = 0;
    time_t last_jpg_time = 0;
    bool is_stream_started = false;

    avformat_network_init();

    // 1. 打开输入流 (根据要求强制 TCP)
    AVDictionary* in_opts = nullptr;
    av_dict_set(&in_opts, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_opts) < 0) return -1;
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }

    // 2. 初始化解码器 (为了存图)
    AVCodecParameters* pCodecPar = ifmt_ctx->streams[video_idx]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);
    pDecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pDecCtx, pCodecPar);
    avcodec_open2(pDecCtx, pCodec, nullptr);

    
    // 在主程序中这样获取
    double frame_rate = av_q2d(ifmt_ctx->streams[video_idx]->avg_frame_rate);
    if (frame_rate <= 0) frame_rate = 16.6; // 如果获取失败，用实测值保底
    if (ifmt_ctx->streams[video_idx]->avg_frame_rate.num > 0) 
        frame_rate = av_q2d(ifmt_ctx->streams[video_idx]->avg_frame_rate);

    // 3. 辅助函数：创建新视频段
    auto create_new_segment = [&](int index) {
        if (ofmt_ctx) {
            av_write_trailer(ofmt_ctx);
            avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            ofmt_ctx = nullptr;
        }
        frame_count_in_seg = 0;
        std::string filename = "video_60s_" + std::to_string(index) + ".mp4";
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);
        out_stream->codecpar->codec_tag = 0;
        avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
        
        AVDictionary* out_opts = nullptr;
        av_dict_set(&out_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        avformat_write_header(ofmt_ctx, &out_opts);
        av_dict_free(&out_opts);
        std::cout << "\n[视频] 开启新片段: " << filename << std::endl;
    };

    // 4. 主循环
    while (av_read_frame(ifmt_ctx, pPkt) >= 0) {
        if (pPkt->stream_index == video_idx) {
            bool is_key = (pPkt->flags & AV_PKT_FLAG_KEY);

            // --- 任务 A: 视频录制逻辑 ---
            if (!is_stream_started) {
                if (is_key) {
                    is_stream_started = true;
                    create_new_segment(file_count++);
                }
            }

            if (is_stream_started) {
                // 判断 60秒 切片
                if (frame_count_in_seg / frame_rate >= SEGMENT_DURATION && is_key) {
                    create_new_segment(file_count++);
                }

                // 复制一份 Packet 用于写入文件 (av_packet_ref 增加引用计数，不影响原包)
                AVPacket *write_pkt = av_packet_clone(pPkt);
                AVStream *out_s = ofmt_ctx->streams[0];
                int64_t pts_val = (int64_t)(frame_count_in_seg * (out_s->time_base.den / (out_s->time_base.num * frame_rate)));
                write_pkt->pts = write_pkt->dts = pts_val;
                write_pkt->duration = (int64_t)(out_s->time_base.den / (out_s->time_base.num * frame_rate));
                write_pkt->stream_index = 0;

                av_interleaved_write_frame(ofmt_ctx, write_pkt);
                av_packet_free(&write_pkt);
                frame_count_in_seg++;
            }

            // --- 任务 B: 5秒抓图逻辑 ---
            // 直接将原包 pPkt 发送给解码器
            if (avcodec_send_packet(pDecCtx, pPkt) == 0) {
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    time_t now = time(nullptr);
                    if (now - last_jpg_time >= JPG_INTERVAL && is_key) {
                        std::cout << "[抓拍] 更新 latest.jpg" << std::endl;
                        save_frame_to_jpg(pFrame, "latest_tmp.jpg");
                        rename("latest_tmp.jpg", "latest.jpg");
                        last_jpg_time = now;
                    }
                }
            }
            printf("\r正在运行: %.2f 秒", frame_count_in_seg / frame_rate);
            fflush(stdout);
        }
        av_packet_unref(pPkt);
    }

    // 清理
    if (ofmt_ctx) av_write_trailer(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);
    avcodec_free_context(&pDecCtx);
    av_frame_free(&pFrame);
    av_packet_free(&pPkt);

    return 0;
}

/*
g++ main_combined.cpp -o main_app -lavformat -lavcodec -lavutil
*/