#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVPacket pkt;
    int video_idx = -1;
    int file_count = 0;

    const int SEGMENT_DURATION = 60; 
    int64_t pts_offset = 0, dts_offset = 0;
    int64_t last_dts = -1;           
    bool is_first_packet_of_seg = true;
    bool is_stream_started = false;

    avformat_network_init();

    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, nullptr) < 0) return -1;
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }

    auto create_new_segment = [&](int index) {
        if (ofmt_ctx) {
            av_write_trailer(ofmt_ctx);
            avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
            ofmt_ctx = nullptr;
        }
        last_dts = -1; 
        std::string filename = "video_60s_" + std::to_string(index) + ".mp4";
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);
        out_stream->codecpar->codec_tag = 0;

        if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;
        
        avformat_write_header(ofmt_ctx, nullptr);
        std::cout << "[任务执行] 开启新片段: " << filename << std::endl;
    };

    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == video_idx) {
            
            if (pkt.pts == AV_NOPTS_VALUE) pkt.pts = pkt.dts;
            if (pkt.dts == AV_NOPTS_VALUE) pkt.dts = pkt.pts;

            bool is_keyframe = (pkt.flags & AV_PKT_FLAG_KEY);

            if (!is_stream_started) {
                if (!is_keyframe) {
                    av_packet_unref(&pkt);
                    continue;
                }
                is_stream_started = true;
                create_new_segment(file_count++);
            }

            AVStream *in_s = ifmt_ctx->streams[video_idx];
            // 计算当前包在原始流中的相对秒数
            double current_sec = (pkt.pts - pts_offset) * av_q2d(in_s->time_base);

            if (current_sec >= SEGMENT_DURATION && is_keyframe) {
                create_new_segment(file_count++);
                is_first_packet_of_seg = true; 
            }

            if (is_first_packet_of_seg) {
                pts_offset = pkt.pts;
                dts_offset = pkt.dts;
                is_first_packet_of_seg = false;
            }

            // 时间戳偏移计算
            pkt.pts -= pts_offset;
            pkt.dts -= dts_offset;

            // --- 关键修正：使用标准的 av_rescale_q ---
            AVStream *out_s = ofmt_ctx->streams[0];
            pkt.pts = av_rescale_q(pkt.pts, in_s->time_base, out_s->time_base);
            pkt.dts = av_rescale_q(pkt.dts, in_s->time_base, out_s->time_base);
            pkt.duration = av_rescale_q(pkt.duration, in_s->time_base, out_s->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;

            // 强制 DTS 递增，解决报错
            if (last_dts != -1 && pkt.dts <= last_dts) {
                pkt.dts = last_dts + 1;
            }
            if (pkt.pts < pkt.dts) pkt.pts = pkt.dts;
            last_dts = pkt.dts;

            if (ofmt_ctx) {
                av_interleaved_write_frame(ofmt_ctx, &pkt);
            }
        }
        av_packet_unref(&pkt);
    }

    if (ofmt_ctx) av_write_trailer(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);
    return 0;
}

/*
g++ main_4_pro.cpp -o main_4_pro -lavformat -lavcodec -lavutil
*/

// #include <iostream>   //可以正常的拿到视频，就是视频有时候可能会有点花
// #include <string>

// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libavutil/avutil.h>
// }

// int main() {
//     const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
//     AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
//     AVPacket pkt;
//     int video_idx = -1;
//     int file_count = 0;

//     const int SEGMENT_DURATION = 60; 
//     int64_t pts_offset = 0, dts_offset = 0;
//     bool is_first_packet_of_seg = true;
//     bool is_stream_started = false;

//     avformat_network_init();

//     if (avformat_open_input(&ifmt_ctx, in_url, nullptr, nullptr) < 0) return -1;
//     if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

//     for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
//         if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//             video_idx = i; break;
//         }
//     }

//     auto create_new_segment = [&](int index) {
//         if (ofmt_ctx) {
//             av_write_trailer(ofmt_ctx);
//             avio_closep(&ofmt_ctx->pb);
//             avformat_free_context(ofmt_ctx);
//             ofmt_ctx = nullptr;
//         }
//         std::string filename = "video_60s_" + std::to_string(index) + ".mp4";
//         avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        
//         AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
//         avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);
//         out_stream->codecpar->codec_tag = 0;

//         if (avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return;

//         AVDictionary* opts = nullptr;
//         av_dict_set(&opts, "movflags", "faststart", 0);
//         avformat_write_header(ofmt_ctx, &opts);
//         std::cout << "[切片通知] 开启新的 60 秒片段: " << filename << std::endl;
//     };

//     while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
//         if (pkt.stream_index == video_idx) {
            
//             // --- 【修复 A】：预处理无效时间戳 ---
//             // 如果 PTS 为空，用 DTS 填充；如果都为空，初始化为 0
//             if (pkt.pts == AV_NOPTS_VALUE) pkt.pts = pkt.dts;
//             if (pkt.dts == AV_NOPTS_VALUE) pkt.dts = pkt.pts;
//             if (pkt.pts == AV_NOPTS_VALUE) { pkt.pts = pkt.dts = 0; }

//             bool is_keyframe = (pkt.flags & AV_PKT_FLAG_KEY);

//             if (!is_stream_started) {
//                 if (!is_keyframe) {
//                     av_packet_unref(&pkt);
//                     continue;
//                 }
//                 is_stream_started = true;
//                 create_new_segment(file_count++);
//             }

//             AVStream *in_s = ifmt_ctx->streams[video_idx];
//             double current_sec = 0;
//             if (!is_first_packet_of_seg) {
//                 current_sec = (pkt.pts - pts_offset) * av_q2d(in_s->time_base);
//             }

//             if (current_sec >= SEGMENT_DURATION && is_keyframe) {
//                 create_new_segment(file_count++);
//                 is_first_packet_of_seg = true; 
//             }

//             if (is_first_packet_of_seg) {
//                 pts_offset = pkt.pts;
//                 dts_offset = pkt.dts;
//                 is_first_packet_of_seg = false;
//             }

//             // --- 【修复 B】：安全的偏移计算 ---
//             pkt.pts -= pts_offset;
//             pkt.dts -= dts_offset;

//             AVStream *out_s = ofmt_ctx->streams[0];
//             pkt.pts = av_rescale_q(pkt.pts, in_s->time_base, out_s->time_base);
//             pkt.dts = av_rescale_q(pkt.dts, in_s->time_base, out_s->time_base);
//             pkt.duration = av_rescale_q(pkt.duration, in_s->time_base, out_s->time_base);
//             pkt.pos = -1;
//             pkt.stream_index = 0;

//             // --- 【修复 C】：强制单调递增检查 ---
//             // 防止由于 B 帧乱序导致的 DTS 大于 PTS 报错
//             if (pkt.pts < pkt.dts) pkt.pts = pkt.dts;

//             if (ofmt_ctx) {
//                 // 使用这个函数可以更好地处理时间戳交错问题
//                 av_interleaved_write_frame(ofmt_ctx, &pkt);
//             }
//         }
//         av_packet_unref(&pkt);
//     }

//     if (ofmt_ctx) av_write_trailer(ofmt_ctx);
//     avformat_close_input(&ifmt_ctx);
//     return 0;
// }

// /*
// g++ main_4_pro.cpp -o main_4_pro -lavformat -lavcodec -lavutil
// ./main_4_pro
// */