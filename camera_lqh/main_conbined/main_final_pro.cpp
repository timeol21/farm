#include <iostream>
#include <string>
#include <ctime>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

// 数据包队列，用于线程间通信
std::queue<AVPacket*> video_queue;
std::queue<AVPacket*> jpg_queue;
std::mutex mtx;
std::condition_variable cv;
bool quit = false;

// --- 抓拍线程：专门负责解压并存 JPG ---
void jpg_worker(AVCodecParameters* codecpar) {
    AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, codecpar);
    avcodec_open2(ctx, codec, nullptr);

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = nullptr;
    time_t last_save = 0;

    while (!quit) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [] { return !jpg_queue.empty() || quit; });
            if (quit && jpg_queue.empty()) break;
            pkt = jpg_queue.front();
            jpg_queue.pop();
        }

        if (avcodec_send_packet(ctx, pkt) == 0) {
            while (avcodec_receive_frame(ctx, frame) == 0) {
                time_t now = time(nullptr);
                // 每 5 秒，且是关键帧时存一次图
                if (now - last_save >= 5 && (pkt->flags & AV_PKT_FLAG_KEY)) {
                    // 编码存图逻辑 (此处简化，直接调用之前的存图函数即可)
                    AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
                    AVCodecContext* e_ctx = avcodec_alloc_context3(enc);
                    e_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
                    e_ctx->width = frame->width; e_ctx->height = frame->height;
                    e_ctx->time_base = {1, 25};
                    if (avcodec_open2(e_ctx, enc, nullptr) >= 0) {
                        AVPacket* j_pkt = av_packet_alloc();
                        if (avcodec_send_frame(e_ctx, frame) == 0 && avcodec_receive_packet(e_ctx, j_pkt) == 0) {
                            FILE* f = fopen("latest_tmp.jpg", "wb");
                            if (f) { fwrite(j_pkt->data, 1, j_pkt->size, f); fclose(f); }
                            rename("latest_tmp.jpg", "latest.jpg");
                            last_save = now;
                            std::cout << "\n[抓拍] 更新 latest.jpg" << std::endl;
                        }
                        av_packet_free(&j_pkt);
                    }
                    avcodec_free_context(&e_ctx);
                }
            }
        }
        av_packet_free(&pkt);
    }
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    avformat_network_init();

    AVFormatContext *ifmt_ctx = nullptr;
    AVDictionary* in_opts = nullptr;
    av_dict_set(&in_opts, "rtsp_transport", "tcp", 0);
    // 增加缓冲区，防止 CSeq 报错
    av_dict_set(&in_opts, "buffer_size", "2048000", 0);

    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_opts) < 0) return -1;
    avformat_find_stream_info(ifmt_ctx, nullptr);

    int v_idx = -1;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++)
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { v_idx = i; break; }

    // 启动抓拍子线程
    std::thread jpg_thread(jpg_worker, ifmt_ctx->streams[v_idx]->codecpar);

    AVFormatContext* ofmt_ctx = nullptr;
    int file_count = 0;
    int64_t frame_count = 0;
    double frame_rate = 16.6; // 使用你实测的 FPS

    auto create_seg = [&](int idx) {
        if (ofmt_ctx) { av_write_trailer(ofmt_ctx); avio_closep(&ofmt_ctx->pb); avformat_free_context(ofmt_ctx); }
        std::string name = "video_60s_" + std::to_string(idx) + ".mp4";
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, name.c_str());
        AVStream* out_s = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_s->codecpar, ifmt_ctx->streams[v_idx]->codecpar);
        out_s->codecpar->codec_tag = 0;
        avio_open(&ofmt_ctx->pb, name.c_str(), AVIO_FLAG_WRITE);
        
        AVDictionary* opts = nullptr;
        // 关键：增加 isml 和 flush_packets 确保实时观看
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof+isml", 0);
        avformat_write_header(ofmt_ctx, &opts);
        std::cout << "\n[视频] 开启新片段: " << name << std::endl;
    };

    AVPacket pkt;
    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == v_idx) {
            bool is_key = (pkt.flags & AV_PKT_FLAG_KEY);
            if (frame_count == 0 && !is_key) { av_packet_unref(&pkt); continue; }

            // 1. 写视频逻辑 (主线程快速完成)
            if (frame_count == 0 || (frame_count / frame_rate >= 60 && is_key)) {
                create_seg(file_count++);
                if (frame_count > 0) frame_count = 0;
            }

            AVStream *out_s = ofmt_ctx->streams[0];
            int64_t pts_v = (int64_t)(frame_count * (out_s->time_base.den / (out_s->time_base.num * frame_rate)));
            pkt.pts = pkt.dts = pts_v;
            pkt.duration = (int64_t)(out_s->time_base.den / (out_s->time_base.num * frame_rate));
            pkt.stream_index = 0;
            av_interleaved_write_frame(ofmt_ctx, &pkt);
            frame_count++;

            // 2. 分发给抓拍线程
            AVPacket* copy_pkt = av_packet_clone(&pkt);
            {
                std::lock_guard<std::mutex> lock(mtx);
                jpg_queue.push(copy_pkt);
                cv.notify_one();
            }
            printf("\r正在录制: %.2f 秒", frame_count / frame_rate); fflush(stdout);
        }
        av_packet_unref(&pkt);
    }

    quit = true; cv.notify_all(); jpg_thread.join();
    return 0;
}

/*
g++ main_final_pro.cpp -o main_app -lavformat -lavcodec -lavutil -lpthread
*/