#include "CameraHandler.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>

extern "C" {
#include <libavutil/opt.h>
}
// 构造函数：初始化参数，初始化FFmpeg网络模块
CameraHandler::CameraHandler(const std::string& rtspUrl, const std::string& name, int jpgInterval)
    : rtspUrl_(rtspUrl), name_(name), jpgInterval_(jpgInterval) {
    avformat_network_init();
}
// 析构函数：调用stop()，确保对象销毁时停止所有任务、释放资源
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

// 实现时间戳生成函数，格式：YYYYMMDD_HHMMSS
std::string CameraHandler::getTimeStamp() {
    // 获取当前系统时间
    auto now = std::chrono::system_clock::now();
    // 转换为本地时间
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTm = *std::localtime(&nowTime);

    // 格式化时间：YYYYMMDD_HHMMSS，补0对齐（比如1月补为01，9分补为09）
    std::ostringstream oss;
    oss << std::setfill('0') 
        << std::setw(4) << localTm.tm_year + 1900 // 年：tm_year是从1900开始的偏移量
        << std::setw(2) << localTm.tm_mon + 1    // 月：tm_mon是0-11，加1为实际月份
        << std::setw(2) << localTm.tm_mday       // 日
        << "_"
        << std::setw(2) << localTm.tm_hour       // 时
        << std::setw(2) << localTm.tm_min        // 分
        << std::setw(2) << localTm.tm_sec;       // 秒
    return oss.str();
}
// ===================== 录制逻辑 (Thread 1) =====================
// ===================== 录制逻辑 (Thread 1 - 增强稳定性版) =====================
void CameraHandler::recordLoop() {
    AVFormatContext *ifmt = nullptr, *ofmt = nullptr;
    AVPacket* pkt = av_packet_alloc();
    int video_idx = -1;

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

    // --- 记录真实系统时间的变量 (用于保底切片) ---
    int64_t last_segment_real_time = av_gettime() / 1000000; 

    auto open_new_mp4 = [&]() {
        if (ofmt) {
            av_write_trailer(ofmt);
            avio_closep(&ofmt->pb);
            avformat_free_context(ofmt);
            ofmt = nullptr;
        }
        std::string filename = name_ + "_rec_" + getTimeStamp() + ".mp4";
        avformat_alloc_output_context2(&ofmt, nullptr, nullptr, filename.c_str());
        AVStream* out_s = avformat_new_stream(ofmt, nullptr);
        avcodec_parameters_copy(out_s->codecpar, ifmt->streams[video_idx]->codecpar);
        out_s->codecpar->codec_tag = 0;

        if (avio_open(&ofmt->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;

        AVDictionary* out_opts = nullptr;
        // 保持 frag_keyframe 确保文件在异常断电时也能最大限度保留数据
        av_dict_set(&out_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        if (avformat_write_header(ofmt, &out_opts) < 0) {
            av_dict_free(&out_opts);
            return;
        }
        av_dict_free(&out_opts);
        std::cout << "[" << name_ << "] 开始新片段: " << filename << std::endl;
    };

    open_new_mp4();
    int64_t start_pts = -1;
    int64_t last_dts = -1;
    int64_t manual_pts_inc = 0; // 用于补全缺失时间戳的计数器

    while (running_ && av_read_frame(ifmt, pkt) >= 0) {
        if (pkt->stream_index == video_idx) {
            AVStream *in_s = ifmt->streams[video_idx];
            AVStream *out_s = ofmt->streams[0];

            // 1. 【补全逻辑】如果摄像头包没带 PTS，根据帧率手动造一个，防止报错
            if (pkt->pts == AV_NOPTS_VALUE) {
                double fps = av_q2d(in_s->avg_frame_rate);
                if (fps < 1) fps = 25; 
                pkt->pts = pkt->dts = manual_pts_inc;
                pkt->duration = in_s->time_base.den / (in_s->time_base.num * fps);
                manual_pts_inc += pkt->duration;
            }

            if (start_pts == -1) start_pts = pkt->pts;

            // 2. 【双重检查】计算流逝的时间
            // A: 基于视频包 PTS 的逻辑秒数
            double elapsed_pts = (pkt->pts - start_pts) * av_q2d(in_s->time_base);
            // B: 基于系统时钟的真实秒数
            int64_t now_real_time = av_gettime() / 1000000;
            double elapsed_real = (double)(now_real_time - last_segment_real_time);

            // 满足任意一个 60 秒且是关键帧，就切片
            if ((elapsed_pts >= 60 || elapsed_real >= 60) && (pkt->flags & AV_PKT_FLAG_KEY)) {
                open_new_mp4();
                start_pts = pkt->pts;
                last_dts = -1;
                last_segment_real_time = now_real_time; // 重置真实时间
            }

            // 3. 时间戳转换与修正 (修正 non monotonically increasing dts)
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
    // 1. 定义FFmpeg核心对象：输入上下文、包、帧（解码后的图像）
    AVFormatContext* ifmt = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();// 解码后的视频帧（YUV格式）
    int64_t last_snap = 0;// 上一次抓拍的时间（秒）
    // 2. 配置RTSP参数：强制TCP（和录制线程一致，保证稳定性）
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    // 打开RTSP + 获取流信息（和录制线程独立，双线程各自连接RTSP，避免共享资源）
    if (avformat_open_input(&ifmt, rtspUrl_.c_str(), nullptr, &opts) < 0) return;
    avformat_find_stream_info(ifmt, nullptr);
    // 3. 初始化解码器：找到最佳视频流 → 找到对应解码器 → 初始化解码上下文
    int v_idx = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);// 简化的流筛选
    const AVCodec* dec = avcodec_find_decoder(ifmt->streams[v_idx]->codecpar->codec_id);// 找到解码器
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);// 分配解码上下文
    avcodec_parameters_to_context(dec_ctx, ifmt->streams[v_idx]->codecpar);// 复制编码参数到解码上下文
    avcodec_open2(dec_ctx, dec, nullptr);// 打开解码器
    // 4. 主循环：运行状态为true，且能读取RTSP包，则持续检测抓拍条件
    while (running_ && av_read_frame(ifmt, pkt) >= 0) {
        if (pkt->stream_index == v_idx) {// 只处理视频流
            // 4.1 获取当前时间（秒）：av_gettime()返回微秒，转秒
            int64_t now = av_gettime() / 1000000;
            // 抓拍条件：达到抓拍间隔 + 当前包是关键帧（AV_PKT_FLAG_KEY）
            if (now - last_snap >= jpgInterval_ && (pkt->flags & AV_PKT_FLAG_KEY)) {
                if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                    // 4.2 解码：发送包到解码器 → 接收解码后的帧
                    if (avcodec_receive_frame(dec_ctx, frame) == 0) {
                        // 4.3 生成文件名：name_latest.jpg（最新图片，覆盖式更新）
                        std::string path = name_ + "_latest.jpg";
                        std::string tmp = path + ".tmp";// 临时文件
                        // 4.4 保存为JPG：先写入临时文件，再原子替换（避免文件损坏）
                        if (saveFrameAsJpeg(frame, tmp)) {
                            rename(tmp.c_str(), path.c_str()); // 原子替换：系统级操作，瞬间完成
                            last_snap = now;// 更新上一次抓拍时间
                            std::cout << "[" << name_ << "] 更新图片: " << path << std::endl;
                        }
                    }
                }
            }
        }
        av_packet_unref(pkt);// 重置包，重复使用
    }
    // 5. 循环退出后：释放所有FFmpeg资源（逆序申请）
    avcodec_free_context(&dec_ctx);// 释放解码上下文
    av_frame_free(&frame);// 释放帧对象
    av_packet_free(&pkt);// 释放包对象
    avformat_close_input(&ifmt);// 关闭RTSP输入流
}

// ===================== 图片编码辅助函数 =====================
bool CameraHandler::saveFrameAsJpeg(AVFrame* frame, const std::string& path) {
    // 1. 找到JPEG编码器（MJPEG是JPEG的视频编码格式，适合单帧编码）
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    // 2. 分配并初始化编码上下文
    AVCodecContext* ctx = avcodec_alloc_context3(enc);
    ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;// MJPEG编码器的标准像素格式（和视频帧的YUV420P兼容）
    ctx->width = frame->width;// 编码宽度（和视频帧一致）
    ctx->height = frame->height;// 编码高度（和视频帧一致）
    ctx->time_base = {1, 25};// 时间基（单帧编码，无实际意义，随便设）
    // 3. 打开编码器
    if (avcodec_open2(ctx, enc, nullptr) < 0) { avcodec_free_context(&ctx); return false; }
    // 4. 编码：将YUV帧编码为JPEG包
    AVPacket* pkt = av_packet_alloc();
    bool success = false;
    // 发送帧到编码器 → 接收编码后的JPEG包
    if (avcodec_send_frame(ctx, frame) == 0 && avcodec_receive_packet(ctx, pkt) == 0) {
        // 5. 写入文件：二进制写模式（wb），JPEG是二进制文件
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { fwrite(pkt->data, 1, pkt->size, f); // 写入JPEG数据
            fclose(f); 
            success = true; }
    }
    // 6. 释放资源
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return success;
}