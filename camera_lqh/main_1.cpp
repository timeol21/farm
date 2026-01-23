#include <iostream>   //没有解码的拉流
#include <string>

// FFmpeg 是 C 语言库，在 C++ 中包含时必须使用 extern "C"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
}

int main() {
    // 1. 定义摄像头的 RTSP 地址
    const char* rtsp_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";

    // 格式上下文，它是 FFmpeg 管理输入输出的核心结构
    AVFormatContext* pFormatCtx = nullptr;
    
    // 初始化网络库
    avformat_network_init();

    // 2. 配置打开选项
    AVDictionary* options = nullptr;
    // 强制使用 TCP 传输，防止 UDP 丢包导致花屏
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    // 设置超时时间（单位：微秒），这里设为 5 秒
    av_dict_set(&options, "stimeout", "5000000", 0);

    // 3. 打开输入流 (这是“函数拉流”的第一步)
    std::cout << "正在尝试连接摄像头: " << rtsp_url << std::endl;
    int ret = avformat_open_input(&pFormatCtx, rtsp_url, nullptr, &options);
    if (ret < 0) {
        char err_buf[1024];
        av_strerror(ret, err_buf, sizeof(err_buf));
        std::cerr << "无法打开输入流: " << err_buf << std::endl;
        return -1;
    }

    // 4. 查找流信息（获取视频的分辨率、编码格式等）
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        std::cerr << "无法找到流信息" << std::endl;
        return -1;
    }

    // 打印视频详情到控制台
    av_dump_format(pFormatCtx, 0, rtsp_url, 0);

    // 5. 循环拉取数据包
    AVPacket pkt;
    av_init_packet(&pkt);

    std::cout << "\n开始拉流... 按 Ctrl+C 停止\n" << std::endl;

    while (true) {
        // av_read_frame 就是从网络中“拉”出一个完整的数据包 (Packet)
        ret = av_read_frame(pFormatCtx, &pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                std::cout << "流已结束" << std::endl;
            } else {
                std::cerr << "读取错误，尝试重连..." << std::endl;
            }
            break;
        }

        // --- 这里是以后多线程处理的核心位置 ---
        if (pFormatCtx->streams[pkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // 我们拿到了视频数据包！
            // 此时 pkt.data 存的就是 H.264/H.265 的压缩数据
            std::cout << "读取到视频包 | 大小: " << pkt.size << " 字节 | PTS: " << pkt.pts << std::endl;
            
            /* 未来在这里：
               1. 线程 A：调用 av_interleaved_write_frame 写入 MP4 文件
               2. 线程 B：通过内存复制将 pkt 发送给连接的用户
            */
        }

        // 释放数据包引用的内存，准备读取下一个
        av_packet_unref(&pkt);
    }

    // 6. 收尾工作
    avformat_close_input(&pFormatCtx);
    avformat_network_deinit();

    return 0;
}

/*
# 确保你已经安装了 libavformat-dev 等库
g++ main_1.cpp -o main_1_app -lavformat -lavcodec -lavutil
*/


/*

main_2.cpp
#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVPacket pkt;
    int ret, video_stream_idx = -1;
    int file_index = 0;
    int64_t start_pts = 0;
    const int segment_duration = 60; // 60秒切片一次

    avformat_network_init();

    // 1. 打开输入
    AVDictionary* in_options = nullptr;
    av_dict_set(&in_options, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_options) < 0) return -1;
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

    // 找到视频流索引
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    auto create_output_file = [&](int index) -> int {
        std::string filename = "seg_" + std::to_string(index) + ".mp4";
        std::cout << "\n--- 创建新文件: " << filename << " ---" << std::endl;
        
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        
        // 只把视频流拷贝过去
        AVStream *in_stream = ifmt_ctx->streams[video_stream_idx];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        out_stream->codecpar->codec_tag = 0;

        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return -1;
        }
        return avformat_write_header(ofmt_ctx, nullptr);
    };

    // 初始化第一个文件
    create_output_file(file_index++);

    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == video_stream_idx) {
            AVStream *in_stream = ifmt_ctx->streams[video_stream_idx];
            AVStream *out_stream = ofmt_ctx->streams[0];

            // 计算当前时间（秒）
            double current_time = pkt.pts * av_q2d(in_stream->time_base);
            if (start_pts == 0) start_pts = pkt.pts;
            double elapsed_time = (pkt.pts - start_pts) * av_q2d(in_stream->time_base);

            // 检查是否到了 60 秒，并且是关键帧
            if (elapsed_time >= segment_duration && (pkt.flags & AV_PKT_FLAG_KEY)) {
                av_write_trailer(ofmt_ctx);
                avio_closep(&ofmt_ctx->pb);
                avformat_free_context(ofmt_ctx);

                create_output_file(file_index++);
                start_pts = pkt.pts; // 重置计时
            }

            // 时间戳转换：从输入流基数转到输出流基数
            pkt.pts = av_rescale_q_nd(pkt.pts - start_pts, in_stream->time_base, out_stream->time_base);
            pkt.dts = av_rescale_q_nd(pkt.dts - start_pts, in_stream->time_base, out_stream->time_base);
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;

            av_interleaved_write_frame(ofmt_ctx, &pkt);
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);
    return 0;
}
*/