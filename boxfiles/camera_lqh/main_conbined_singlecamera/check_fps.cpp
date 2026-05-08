#include <iostream>
#include <vector>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

int main() {
    const char* url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    avformat_network_init();

    AVFormatContext* pFormatCtx = nullptr;
    AVDictionary* options = nullptr;
    // 按照你的要求，强制使用 TCP
    av_dict_set(&options, "rtsp_transport", "tcp", 0);

    std::cout << "正在连接摄像头并分析流信息..." << std::endl;
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) {
        std::cerr << "无法打开流" << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) return -1;

    int video_idx = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
            break;
        }
    }

    if (video_idx == -1) {
        std::cerr << "未找到视频流" << std::endl;
        return -1;
    }

    // --- 打印摄像头声明的帧率 ---
    AVStream* v_stream = pFormatCtx->streams[video_idx];
    std::cout << "\n[系统声明帧率] " << av_q2d(v_stream->avg_frame_rate) << " FPS" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "现在开始实测（采集 5 秒数据）..." << std::endl;

    AVPacket pkt;
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < end_time) {
        if (av_read_frame(pFormatCtx, &pkt) >= 0) {
            if (pkt.stream_index == video_idx) {
                frame_count++;
                if (frame_count % 10 == 0) {
                    std::cout << "." << std::flush;
                }
            }
            av_packet_unref(&pkt);
        }
    }

    auto actual_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = actual_end - start_time;

    // --- 计算实测帧率 ---
    double actual_fps = frame_count / elapsed.count();

    std::cout << "\n\n[测试结果]" << std::endl;
    std::cout << "总计用时: " << elapsed.count() << " 秒" << std::endl;
    std::cout << "总计收到帧数: " << frame_count << std::endl;
    std::cout << ">> 实测平均帧率: " << actual_fps << " FPS <<" << std::endl;
    std::cout << "------------------------------------------" << std::endl;

    if (actual_fps > 23 && actual_fps < 27) {
        std::cout << "建议设置值: 25.0" << std::endl;
    } else if (actual_fps > 28 && actual_fps < 32) {
        std::cout << "建议设置值: 30.0" << std::endl;
    } else if (actual_fps > 18 && actual_fps < 22) {
        std::cout << "建议设置值: 20.0" << std::endl;
    } else {
        std::cout << "建议根据实测值微调你的 frame_rate 变量。" << std::endl;
    }

    avformat_close_input(&pFormatCtx);
    return 0;
}

/*
g++ check_fps.cpp -o check_fps -lavformat -lavcodec -lavutil
./check_fps
*/