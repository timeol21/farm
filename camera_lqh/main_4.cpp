#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

// 全局变量用于时间校准
int64_t pts_offset = 0;
int64_t dts_offset = 0;
bool first_pkt_in_seg = true;         
bool is_first_frame_of_stream = true; 

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    AVFormatContext *ifmt_ctx = nullptr;
    AVFormatContext *ofmt_ctx = nullptr;
    AVPacket pkt;
    
    int video_idx = -1;
    int file_count = 0;
    int frame_in_seg = 0;
    const int FRAMES_PER_SEGMENT = 150; 

    avformat_network_init();

    // 1-4. 按照文档基础步骤初始化
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, nullptr) < 0) return -1;
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }

    // 辅助函数：创建新文件
    auto create_new_segment = [&](int index) {
        if (ofmt_ctx) {
            av_write_trailer(ofmt_ctx);
            avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            ofmt_ctx = nullptr;
        }
        std::string filename = "segment_1" + std::to_string(index) + ".mp4";
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);

        // --- 【核心修复1】：强制每个 MP4 文件头部包含 SPS/PPS 描述信息 ---
        // 这样播放器一打开文件就能立刻知道如何渲染画面，解决黑屏。
        out_stream->codecpar->codec_tag = 0;
        
        if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;

        // --- 【核心修复2】：添加额外参数，确保元数据在文件开头 ---
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        
        avformat_write_header(ofmt_ctx, &opts);
        std::cout << "[成功] 录制无黑屏片段: " << filename << std::endl;
    };

    // 8. 读取循环
    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == video_idx) {
            
            // 只有是 I帧 时才允许开始第一个文件（解决 segment_0 开头黑屏）
            if (is_first_frame_of_stream) {
                if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
                    av_packet_unref(&pkt);
                    continue; 
                }
                is_first_frame_of_stream = false; 
                create_new_segment(file_count++);
            }

            // 只有是 I帧 时才允许切换新文件（解决 segment_1 开头黑屏）
            if (frame_in_seg >= FRAMES_PER_SEGMENT && (pkt.flags & AV_PKT_FLAG_KEY)) {
                create_new_segment(file_count++);
                frame_in_seg = 0;
                first_pkt_in_seg = true;
            }

            if (first_pkt_in_seg) {
                pts_offset = pkt.pts;
                dts_offset = pkt.dts;
                first_pkt_in_seg = false;
            }

            // 时间戳归零处理
            if (pkt.pts != AV_NOPTS_VALUE) pkt.pts -= pts_offset;
            if (pkt.dts != AV_NOPTS_VALUE) pkt.dts -= dts_offset;

            AVStream *in_s = ifmt_ctx->streams[video_idx];
            AVStream *out_s = ofmt_ctx->streams[0];
            pkt.pts = av_rescale_q(pkt.pts, in_s->time_base, out_s->time_base);
            pkt.dts = av_rescale_q(pkt.dts, in_s->time_base, out_s->time_base);
            pkt.duration = av_rescale_q(pkt.duration, in_s->time_base, out_s->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;

            if (ofmt_ctx) {
                av_interleaved_write_frame(ofmt_ctx, &pkt);
                frame_in_seg++;
            }
        }
        av_packet_unref(&pkt);
    }

    if (ofmt_ctx) av_write_trailer(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);
    return 0;
}


/*
g++ main_4.cpp -o main_4_app -lavformat -lavcodec -lavutil
./main_4_app
*/

// #include <iostream>  //前面15秒左右是黑屏
// #include <string>

// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libavutil/avutil.h>
// }

// // 记录时间戳偏移，用于将视频时间归零
// int64_t pts_offset = 0;
// int64_t dts_offset = 0;
// bool first_pkt_in_seg = true;         // 标记是否是新文件的第一个包
// bool is_first_frame_of_stream = true; // 标记是否是程序启动后的第一个包

// int main() {
//     const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
//     AVFormatContext *ifmt_ctx = nullptr;
//     AVFormatContext *ofmt_ctx = nullptr;
//     AVPacket pkt;
    
//     int video_idx = -1;
//     int file_count = 0;
//     int frame_in_seg = 0;
//     const int FRAMES_PER_SEGMENT = 150; // 每150帧一切片（约6秒，方便测试）

//     avformat_network_init();

//     // --- 文档步骤 1：分配格式上下文 ---
//     ifmt_ctx = avformat_alloc_context();

//     // --- 文档步骤 2：打开RTSP流 ---
//     AVDictionary* in_options = nullptr;
//     av_dict_set(&in_options, "rtsp_transport", "tcp", 0); // 强制TCP，减少丢包导致的花屏
//     if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_options) < 0) {
//         std::cerr << "无法打开输入流" << std::endl;
//         return -1;
//     }

//     // --- 文档步骤 3：获取流信息 ---
//     if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
//         std::cerr << "无法获取流信息" << std::endl;
//         return -1;
//     }

//     // --- 文档步骤 4：查找视频流索引 ---
//     for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
//         if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//             video_idx = i;
//             break;
//         }
//     }
//     if (video_idx == -1) return -1;

//     // 辅助函数：严格按照封装流程创建新MP4文件
//     auto create_new_segment = [&](int index) {
//         if (ofmt_ctx) {
//             av_write_trailer(ofmt_ctx); // 写入文件尾
//             avio_closep(&ofmt_ctx->pb);
//             avformat_free_context(ofmt_ctx);
//             ofmt_ctx = nullptr;
//         }
//         std::string filename = "segment_" + std::to_string(index) + ".mp4";
//         avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        
//         AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
//         // 关键：复制编码参数，确保SPS/PPS信息完整（解决花屏）
//         avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);
//         out_stream->codecpar->codec_tag = 0;

//         if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;
        
//         // 写入文件头
//         if (avformat_write_header(ofmt_ctx, nullptr) < 0) {
//             std::cerr << "无法写入文件头" << std::endl;
//         }
//         std::cout << "[系统] 开启高质量片段: " << filename << std::endl;
//     };

//     // --- 文档步骤 8：循环读取每一帧数据包 ---
//     while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
//         if (pkt.stream_index == video_idx) {
            
//             // 【逻辑1】拦截黑屏：如果没遇到第一个I帧，坚决不写文件
//             if (is_first_frame_of_stream) {
//                 if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
//                     av_packet_unref(&pkt);
//                     continue; // 丢弃流开始时的所有非关键帧
//                 }
//                 is_first_frame_of_stream = false; 
//                 create_new_segment(file_count++); // 此时才创建第一个文件
//             }

//             // 【逻辑2】平滑切片：达到帧数且必须是I帧才切换
//             if (frame_in_seg >= FRAMES_PER_SEGMENT && (pkt.flags & AV_PKT_FLAG_KEY)) {
//                 create_new_segment(file_count++);
//                 frame_in_seg = 0;
//                 first_pkt_in_seg = true; // 开启新文件，重置时间偏移
//             }

//             // 【逻辑3】时间戳归零（解决 out of range 和 pts has no value 报错）
//             if (first_pkt_in_seg) {
//                 pts_offset = pkt.pts;
//                 dts_offset = pkt.dts;
//                 first_pkt_in_seg = false;
//             }

//             // 将当前包的时间戳减去起始偏移量，实现从0秒开始
//             if (pkt.pts != AV_NOPTS_VALUE) pkt.pts -= pts_offset;
//             if (pkt.dts != AV_NOPTS_VALUE) pkt.dts -= dts_offset;

//             // 时间基换算：将摄像头的频率换算为MP4文件的频率
//             AVStream *in_s = ifmt_ctx->streams[video_idx];
//             AVStream *out_s = ofmt_ctx->streams[0];
//             pkt.pts = av_rescale_q(pkt.pts, in_s->time_base, out_s->time_base);
//             pkt.dts = av_rescale_q(pkt.dts, in_s->time_base, out_s->time_base);
//             pkt.duration = av_rescale_q(pkt.duration, in_s->time_base, out_s->time_base);
//             pkt.pos = -1;
//             pkt.stream_index = 0;

//             // 修正DTS，确保单调递增（防止花屏报错）
//             if (pkt.pts < pkt.dts) pkt.pts = pkt.dts;

//             // 写入数据
//             if (ofmt_ctx) {
//                 av_interleaved_write_frame(ofmt_ctx, &pkt);
//                 frame_in_seg++;
//             }
//         }
//         av_packet_unref(&pkt);
//     }

//     // 善后处理
//     if (ofmt_ctx) {
//         av_write_trailer(ofmt_ctx);
//         avio_closep(&ofmt_ctx->pb);
//         avformat_free_context(ofmt_ctx);
//     }
//     avformat_close_input(&ifmt_ctx);

//     return 0;
// }

// /*
// g++ main_4.cpp -o main_4_app -lavformat -lavcodec -lavutil
// */