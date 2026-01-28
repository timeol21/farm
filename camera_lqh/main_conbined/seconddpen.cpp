#include <iostream>
#include <string>
#include <ctime>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

// 多线程控制变量
std::queue<AVPacket*> snap_queue;
std::mutex queue_mtx;
std::condition_variable queue_cv;
std::atomic<bool> g_running(true);

// 抓拍后台线程逻辑
void snapshot_thread_func(AVCodecParameters* codecpar) {
    const AVCodec* pCodec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* pDecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pDecCtx, codecpar);
    if (avcodec_open2(pDecCtx, pCodec, nullptr) < 0) return;

    AVFrame* pFrame = av_frame_alloc();
    const AVCodec* pEnc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

    while (g_running) {
        AVPacket* pkt = nullptr;
        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            queue_cv.wait(lock, [] { return !snap_queue.empty() || !g_running; });
            if (!g_running && snap_queue.empty()) break;
            pkt = snap_queue.front();
            snap_queue.pop();
        }

        if (pkt) {
            if (avcodec_send_packet(pDecCtx, pkt) == 0) {
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    AVCodecContext* pEncCtx = avcodec_alloc_context3(pEnc);
                    pEncCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; 
                    pEncCtx->width = pFrame->width; pEncCtx->height = pFrame->height;
                    pEncCtx->time_base = {1, 25};
                    if (avcodec_open2(pEncCtx, pEnc, nullptr) >= 0) {
                        AVPacket* jpgPkt = av_packet_alloc();
                        if (avcodec_send_frame(pEncCtx, pFrame) == 0 && avcodec_receive_packet(pEncCtx, jpgPkt) == 0) {
                            FILE* f = fopen("latest_tmp.jpg", "wb");
                            if (f) { 
                                fwrite(jpgPkt->data, 1, jpgPkt->size, f); 
                                fclose(f); 
                                rename("latest_tmp.jpg", "latest.jpg"); 
                            }
                        }
                        av_packet_free(&jpgPkt);
                    }
                    avcodec_free_context(&pEncCtx);
                }
            }
            av_packet_free(&pkt);
        }
    }
    av_frame_free(&pFrame);
    avcodec_free_context(&pDecCtx);
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVPacket *pPkt = av_packet_alloc();
    int video_idx = -1;
    int file_count = 0;
    const int SEGMENT_DURATION = 60; 
    const int JPG_INTERVAL = 5;
    int64_t frame_count_in_seg = 0;
    time_t last_jpg_time = 0;
    bool is_stream_started = false;

    avformat_network_init();

    // 1. 输入配置 (强制 TCP)
    AVDictionary* in_opts = nullptr;
    av_dict_set(&in_opts, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_opts) < 0) return -1;
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }
    if (video_idx == -1) return -1;

    double frame_rate = av_q2d(ifmt_ctx->streams[video_idx]->avg_frame_rate);
    if (frame_rate <= 0) frame_rate = 25.0;

    // 启动抓拍线程
    std::thread snap_thread(snapshot_thread_func, ifmt_ctx->streams[video_idx]->codecpar);

    // 2. 创建视频段 Lambda (优化 VS Code 播放)
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
        
        if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) >= 0) {
            AVDictionary* out_opts = nullptr;
            // 关键：frag_keyframe 和 empty_moov 确保 VS Code 这种基于浏览器的播放器能边读边播
            av_dict_set(&out_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
            
            // 【修复点】接收并检查返回值，消除警告
            int ret = avformat_write_header(ofmt_ctx, &out_opts);
            if (ret < 0) {
                std::cerr << "Error: Could not write header to output file" << std::endl;
            }
            av_dict_free(&out_opts);
        }
        std::cout << "\n[录制中] 产生新文件: " << filename << " (支持 VS Code 实时点开)" << std::endl;
    };

    // 3. 主循环
    while (av_read_frame(ifmt_ctx, pPkt) >= 0) {
        if (pPkt->stream_index == video_idx) {
            bool is_key = (pPkt->flags & AV_PKT_FLAG_KEY);

            if (!is_stream_started && is_key) {
                is_stream_started = true;
                create_new_segment(file_count++);
            }

            if (is_stream_started && ofmt_ctx) {
                if (frame_count_in_seg / frame_rate >= SEGMENT_DURATION && is_key) {
                    create_new_segment(file_count++);
                }

                AVPacket *write_pkt = av_packet_clone(pPkt);
                AVStream *out_s = ofmt_ctx->streams[0];
                
                // 重新计算 PTS/DTS 确保播放器不卡顿
                int64_t pts_val = (int64_t)(frame_count_in_seg * (out_s->time_base.den / (out_s->time_base.num * frame_rate)));
                write_pkt->pts = write_pkt->dts = pts_val;
                write_pkt->duration = (int64_t)(out_s->time_base.den / (out_s->time_base.num * frame_rate));
                write_pkt->stream_index = 0;

                av_interleaved_write_frame(ofmt_ctx, write_pkt);
                
                // 强制刷入磁盘，确保 VS Code 播放器能拿到最新字节
                avio_flush(ofmt_ctx->pb); 
                
                av_packet_free(&write_pkt);
                frame_count_in_seg++;
            }

            // 抓拍逻辑 (不阻塞主线程)
            time_t now = time(nullptr);
            if (now - last_jpg_time >= JPG_INTERVAL && is_key) {
                AVPacket* snap_pkt = av_packet_clone(pPkt);
                {
                    std::lock_guard<std::mutex> lock(queue_mtx);
                    snap_queue.push(snap_pkt);
                }
                queue_cv.notify_one();
                last_jpg_time = now;
            }
            printf("\r当前录制时长: %.2f 秒 | 待处理抓拍: %zu ", frame_count_in_seg / frame_rate, snap_queue.size());
            fflush(stdout);
        }
        av_packet_unref(pPkt);
    }

    // 4. 清理退出
    g_running = false;
    queue_cv.notify_all();
    if (snap_thread.joinable()) snap_thread.join();
    if (ofmt_ctx) {
        av_write_trailer(ofmt_ctx);
        avio_closep(&ofmt_ctx->pb);
        avformat_free_context(ofmt_ctx);
    }
    avformat_close_input(&ifmt_ctx);
    av_packet_free(&pPkt);

    return 0;
}

/*
g++ seconddpen.cpp -o seconddpen -lavformat -lavcodec -lavutil -lpthread
*/