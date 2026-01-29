#include <iostream>   //能实现实验室摄像头的流保存和图片的维护
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

// --- 多线程通信变量 ---
std::queue<AVPacket*> snap_queue;
std::mutex queue_mtx;
std::condition_variable queue_cv;
std::atomic<bool> g_running(true);

// 抓拍编码函数
void save_frame_to_jpg(AVFrame* pFrame, const std::string& filename) {
    const AVCodec* pEnc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!pEnc) return;
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
                if (f) { fwrite(pkt->data, 1, pkt->size, f); fclose(f); }
            }
        }
        av_packet_free(&pkt);
    }
    avcodec_free_context(&pEncCtx);
}

// 抓拍后台线程
void snapshot_thread_func(AVCodecParameters* codecpar) {
    // --- 1. 初始化解码器 (RTSP -> Frame) ---
    const AVCodec* pDec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* pDecCtx = avcodec_alloc_context3(pDec);
    avcodec_parameters_to_context(pDecCtx, codecpar);
    avcodec_open2(pDecCtx, pDec, nullptr);

    // --- 2. 预初始化 JPEG 编码器 (Frame -> JPG) ---
    // 关键修复：把编码器初始化移出 while 循环
    const AVCodec* pEnc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* pEncCtx = avcodec_alloc_context3(pEnc);
    pEncCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    pEncCtx->width = codecpar->width;
    pEncCtx->height = codecpar->height;
    pEncCtx->time_base = {1, 25};
    if (avcodec_open2(pEncCtx, pEnc, nullptr) < 0) {
        std::cerr << "无法打开 JPEG 编码器" << std::endl;
        return;
    }

    AVFrame* pFrame = av_frame_alloc();
    AVPacket* jpgPkt = av_packet_alloc();

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
            // 解码 RTSP 包
            if (avcodec_send_packet(pDecCtx, pkt) == 0) {
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    // 编码为 JPEG
                    if (avcodec_send_frame(pEncCtx, pFrame) == 0) {
                        if (avcodec_receive_packet(pEncCtx, jpgPkt) == 0) {
                            // 写入文件逻辑 (保持原子替换策略)
                            FILE* f = fopen("latest_tmp.jpg", "wb");
                            if (f) {
                                fwrite(jpgPkt->data, 1, jpgPkt->size, f);
                                fclose(f);
                                rename("latest_tmp.jpg", "latest.jpg");
                            }
                            av_packet_unref(jpgPkt); // 必须释放，否则内存溢出
                        }
                    }
                }
            }
            av_packet_free(&pkt);
        }
    }

    // --- 3. 线程退出时统一释放 ---
    av_frame_free(&pFrame);
    av_packet_free(&jpgPkt);
    avcodec_free_context(&pEncCtx);
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

    // 1. 打开输入流 (强制 TCP)
    AVDictionary* in_opts = nullptr;
    av_dict_set(&in_opts, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_opts) < 0) return -1;
    avformat_find_stream_info(ifmt_ctx, nullptr);

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }

    double frame_rate = av_q2d(ifmt_ctx->streams[video_idx]->avg_frame_rate);
    if (frame_rate <= 0) frame_rate = 25.0;

    std::thread snap_thread(snapshot_thread_func, ifmt_ctx->streams[video_idx]->codecpar);

    // 3. 关键修正：创建支持“秒开”和“边录边看”的文件
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
            /**
             * 核心参数说明：
             * frag_keyframe: 每一个关键帧都刷出一个片段（支持实时播放的关键）
             * empty_moov: 让文件头可以后续填充，允许文件在未关闭时被读取
             * default_base_moof: 兼容性优化
             */
            av_dict_set(&out_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof+separate_moof", 0);
            
            if (avformat_write_header(ofmt_ctx, &out_opts) < 0) {
                std::cerr << "Header Error" << std::endl;
            }
            av_dict_free(&out_opts);
        }
        std::cout << "\n[新片段] " << filename << " (支持实时观看)" << std::endl;
    };

    // 4. 主循环
    while (av_read_frame(ifmt_ctx, pPkt) >= 0) {
        if (pPkt->stream_index == video_idx) {
            bool is_key = (pPkt->flags & AV_PKT_FLAG_KEY);

            if (!is_stream_started && is_key) {
                is_stream_started = true;
                create_new_segment(file_count++);
            }

            if (is_stream_started) {
                if (frame_count_in_seg / frame_rate >= SEGMENT_DURATION && is_key) {
                    create_new_segment(file_count++);
                }

                AVPacket *write_pkt = av_packet_clone(pPkt);
                AVStream *out_s = ofmt_ctx->streams[0];
                int64_t pts_val = (int64_t)(frame_count_in_seg * (out_s->time_base.den / (out_s->time_base.num * frame_rate)));
                write_pkt->pts = write_pkt->dts = pts_val;
                write_pkt->duration = (int64_t)(out_s->time_base.den / (out_s->time_base.num * frame_rate));
                write_pkt->stream_index = 0;

                av_interleaved_write_frame(ofmt_ctx, write_pkt);
                // 强制刷新 I/O 缓存，确保播放器能立马读到新数据
                avio_flush(ofmt_ctx->pb); 

                av_packet_free(&write_pkt);
                frame_count_in_seg++;
            }

            time_t now = time(nullptr);
            if (now - last_jpg_time >= JPG_INTERVAL && is_key) {
                AVPacket* snap_pkt = av_packet_clone(pPkt);
                { std::lock_guard<std::mutex> lock(queue_mtx); snap_queue.push(snap_pkt); }
                queue_cv.notify_one();
                last_jpg_time = now;
            }
            printf("\r秒数: %.2f | 抓拍队深: %zu ", frame_count_in_seg / frame_rate, snap_queue.size());
            fflush(stdout);
        }
        av_packet_unref(pPkt);
    }

    g_running = false;
    queue_cv.notify_all();
    if (snap_thread.joinable()) snap_thread.join();
    if (ofmt_ctx) { av_write_trailer(ofmt_ctx); avio_closep(&ofmt_ctx->pb); }
    avformat_close_input(&ifmt_ctx);
    av_packet_free(&pPkt);
    return 0;
}

/*
g++ secondopen.cpp -o secondopen -lavformat -lavcodec -lavutil -lpthread
*/