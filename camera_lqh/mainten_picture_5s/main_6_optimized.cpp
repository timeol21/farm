#include <iostream>    //对应latest.jpg，用来维护每五秒钟更新一次图片的代码
#include <string>
#include <ctime>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

// 严格按照文档编码步骤（找到指定编号编码器MJPEG -> 配置上下文 -> 发送帧 -> 接收包）
void save_frame_to_jpg_standard(AVFrame* pFrame, const std::string& filename);

int main() {
    const char* url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    
    avformat_network_init();

    // --- 文档步骤 1: 分配格式上下文 ---
    AVFormatContext* pFormatCtx = avformat_alloc_context();

    // --- 文档步骤 2: 打开RTSP流 ---
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) return -1;

    // --- 文档步骤 3: 获取流信息 ---
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) return -1;

    // --- 文档步骤 4: 查找视频流索引 ---
    int videoIdx = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = i; break;
        }
    }

    // --- 文档步骤 5: 获取视频流解码器 ---
    AVCodecParameters* pCodecPar = pFormatCtx->streams[videoIdx]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);

    // --- 文档步骤 6: 分配解码器上下文并复制参数 ---
    AVCodecContext* pDecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pDecCtx, pCodecPar);

    // --- 文档步骤 7: 绑定解码器并打开 ---
    if (avcodec_open2(pDecCtx, pCodec, nullptr) < 0) return -1;

    AVPacket* pPkt = av_packet_alloc();
    AVFrame* pFrame = av_frame_alloc();

    time_t last_save_time = time(nullptr);
    bool waiting_for_keyframe = false; // 用于 I 帧锚定的标记
    const int interval = 5; 

    std::cout << "实时监控中：每5秒锚定一个I帧更新 latest.jpg..." << std::endl;

    // --- 文档步骤 8: 循环读取数据包 ---
    while (av_read_frame(pFormatCtx, pPkt) >= 0) {
        if (pPkt->stream_index == videoIdx) {
            
            time_t now = time(nullptr);
            // 如果时间到了5秒，开启“等待关键帧”模式
            if (now - last_save_time >= interval) {
                waiting_for_keyframe = true;
            }

            // --- 文档步骤 9: 发送数据包到解码器 ---
            if (avcodec_send_packet(pDecCtx, pPkt) == 0) {
                
                // --- 文档步骤 10: 接收解码后的帧 ---
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    
                    // 只有时间到了，且当前包是关键帧（I帧），我们才执行保存（解决花屏）
                    if (waiting_for_keyframe && (pPkt->flags & AV_PKT_FLAG_KEY)) {
                        
                        std::cout << "[捕捉] 检测到最新I帧，更新画面..." << std::endl;
                        
                        // 解决读写冲突：先写 tmp，再 rename
                        save_frame_to_jpg_standard(pFrame, "latest_tmp.jpg");
                        rename("latest_tmp.jpg", "latest.jpg");
                        
                        last_save_time = now;
                        waiting_for_keyframe = false; // 重置标记，进入下一个5秒循环
                    }
                    // 如果没到时间，或者不是I帧，我们也要解码（为了保持解码器状态），但不做存图操作
                    // 这就是“跳帧”的思想：解而不存，只取最新。
                }
            }
        }
        av_packet_unref(pPkt);
    }

    // 清理
    av_frame_free(&pFrame);
    av_packet_free(&pPkt);
    avcodec_free_context(&pDecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}

// 严格按照文档最后提到的【编码步骤】
void save_frame_to_jpg_standard(AVFrame* pFrame, const std::string& filename) {
    // 找到指定编号编码器 MJPEG
    AVCodec* pEnc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* pEncCtx = avcodec_alloc_context3(pEnc);
    
    // 配置上下文（必须设置宽、高、像素格式）
    pEncCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; // JPEG标准格式
    pEncCtx->width = pFrame->width;
    pEncCtx->height = pFrame->height;
    pEncCtx->time_base = {1, 25};

    if (avcodec_open2(pEncCtx, pEnc, nullptr) < 0) return;

    AVPacket* pkt = av_packet_alloc();
    
    // 发送帧到编码器
    if (avcodec_send_frame(pEncCtx, pFrame) == 0) {
        // 接收编码后的包
        if (avcodec_receive_packet(pEncCtx, pkt) == 0) {
            FILE* f = fopen(filename.c_str(), "wb");
            if (f) {
                fwrite(pkt->data, 1, pkt->size, f);
                fclose(f);
            }
        }
    }

    av_packet_free(&pkt);
    avcodec_free_context(&pEncCtx);
}

/*
g++ main_6_optimized.cpp -o main_6_app -lavformat -lavcodec -lavutil
./main_6_app

用 watch -n 1 ls -l latest.jpg 或者直接打开图片查看器。
*/