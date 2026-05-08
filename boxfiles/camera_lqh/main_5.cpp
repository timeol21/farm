#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

int main() {
    const char* in_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVCodecContext *pDecCtx = nullptr; // 解码器上下文
    AVPacket pkt;
    AVFrame *pFrame = av_frame_alloc(); // 存放解码后的像素帧
    int video_idx = -1;
    int file_count = 0;
    int frame_in_seg = 0;
    const int FRAMES_PER_SEGMENT = 100; // 每100帧一切片

    avformat_network_init();

    // 【1. 分配格式上下文】 管理流整体信息
    ifmt_ctx = avformat_alloc_context();

    // 【2. 打开RTSP流】 建立连接并读取头部信息
    AVDictionary* in_options = nullptr;
    av_dict_set(&in_options, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&ifmt_ctx, in_url, nullptr, &in_options) < 0) return -1;

    // 【3. 获取流信息】
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) return -1;

    // 【4. 查找视频流索引】
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i; break;
        }
    }

    // 【5-7. 初始化解码器】 为了后续识别，必须先能把包解成帧
    AVCodecParameters* pCodecPar = ifmt_ctx->streams[video_idx]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id); // 5.找解码器
    pDecCtx = avcodec_alloc_context3(pCodec); // 6.分配上下文
    avcodec_parameters_to_context(pDecCtx, pCodecPar); // 复制参数
    avcodec_open2(pDecCtx, pCodec, nullptr); // 7.打开解码器

    // 【辅助函数：创建MP4输出文件】 负责封装层的初始化
    auto create_new_segment = [&](int index) {
        if (ofmt_ctx) {
            av_write_trailer(ofmt_ctx);
            avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
        }
        std::string filename = "seg_" + std::to_string(index) + ".mp4";
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, filename.c_str());
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_idx]->codecpar);
        out_stream->codecpar->codec_tag = 0;
        avio_open(&ofmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
        avformat_write_header(ofmt_ctx, nullptr);
        std::cout << ">>> 开启新视频录制: " << filename << std::endl;
    };

    create_new_segment(file_count++);

    // 【8. 循环读取数据包】
    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == video_idx) {
            
            // --- 逻辑 A：视频切片与存储 (不解码，直接存) ---
            bool is_i_frame = (pkt.flags & AV_PKT_FLAG_KEY);
            if (frame_in_seg >= FRAMES_PER_SEGMENT && is_i_frame) {
                create_new_segment(file_count++);
                frame_in_seg = 0;
            }

            // 时间戳转换，确保MP4文件播放速度正常
            AVStream *in_s = ifmt_ctx->streams[video_idx];
            AVStream *out_s = ofmt_ctx->streams[0];
            AVPacket save_pkt; // 拷贝一份用于存储，防止干扰解码
            av_packet_ref(&save_pkt, &pkt); 
            save_pkt.pts = av_rescale_q(pkt.pts, in_s->time_base, out_s->time_base);
            save_pkt.dts = av_rescale_q(pkt.dts, in_s->time_base, out_s->time_base);
            save_pkt.duration = av_rescale_q(pkt.duration, in_s->time_base, out_s->time_base);
            save_pkt.stream_index = 0;
            av_interleaved_write_frame(ofmt_ctx, &save_pkt);
            av_packet_unref(&save_pkt);
            frame_in_seg++;

            // --- 逻辑 B：解码处理 (送去识别) ---
            // 【9. 发送数据包到解码器】
            if (avcodec_send_packet(pDecCtx, &pkt) == 0) {
                // 【10. 接收解码后的帧】
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    // 到这里，pFrame 就是原始像素数据了！
                    // 以后识别算法的代码就写在这里
                    if (frame_in_seg % 50 == 0) {
                        std::cout << "[识别模块] 已获取一帧画面，分辨率: " 
                                  << pFrame->width << "x" << pFrame->height 
                                  << "，正在后台分析物品..." << std::endl;
                    }
                }
            }
        }
        av_packet_unref(&pkt);
    }

    // 清理释放
    av_write_trailer(ofmt_ctx);
    av_frame_free(&pFrame);
    avcodec_free_context(&pDecCtx);
    avformat_close_input(&ifmt_ctx);
    return 0;
}