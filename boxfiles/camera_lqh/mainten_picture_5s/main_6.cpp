#include <iostream>    //功能残缺的维护照片的代码
#include <string>
#include <ctime>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

// 编码存图函数（对应文档最后的编码步骤）
void save_frame_as_jpg(AVFrame* pFrame, const std::string& filename);

int main() {
    const char* url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    
    avformat_network_init();

    // 1. 分配格式上下文 (文档步骤 1)
    AVFormatContext* pFormatCtx = avformat_alloc_context();

    // 2. 打开RTSP流 (文档步骤 2)
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) return -1;

    // 3. 获取流信息 (文档步骤 3)
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) return -1;

    // 4. 查找视频流索引 (文档步骤 4)
    int videoIdx = -1;
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = i; break;
        }
    }

    // 5. 获取视频流解码器 (文档步骤 5)
    AVCodecParameters* pCodecPar = pFormatCtx->streams[videoIdx]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);

    // 6. 分配解码器上下文并复制参数 (文档步骤 6)
    AVCodecContext* pDecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pDecCtx, pCodecPar);

    // 7. 打开解码器 (文档步骤 7)
    if (avcodec_open2(pDecCtx, pCodec, nullptr) < 0) return -1;

    AVPacket* pPkt = av_packet_alloc();
    AVFrame* pFrame = av_frame_alloc();

    // 记录上一次存图的时间
    time_t last_save_time = 0;
    const int interval = 5; // 设定 5 秒间隔

    std::cout << "程序启动，每 5 秒将更新一次 latest.jpg..." << std::endl;

    // 8. 循环读取数据包 (文档步骤 8)
    while (av_read_frame(pFormatCtx, pPkt) >= 0) {
        if (pPkt->stream_index == videoIdx) {
            
            // 9. 发送数据包到解码器 (文档步骤 9)
            if (avcodec_send_packet(pDecCtx, pPkt) == 0) {
                
                // 10. 接收解码后的帧 (文档步骤 10)
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    
                    time_t now = time(nullptr);
                    // 检查是否到了 5 秒间隔
                    if (now - last_save_time >= interval) {
                        // 执行保存逻辑
                        std::cout << "[更新] 正在采集最新画面..." << std::endl;
                        
                        // 采用临时文件再重命名的策略，防止查看时图片显示一半
                        save_frame_as_jpg(pFrame, "latest_tmp.jpg");
                        rename("latest_tmp.jpg", "latest.jpg");
                        
                        last_save_time = now;
                    }
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

// 严格按照文档编码步骤实现的存图函数
void save_frame_as_jpg(AVFrame* pFrame, const std::string& filename) {
    // 找到 MJPEG 编码器
    AVCodec* pCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    AVCodecContext* pCtx = avcodec_alloc_context3(pCodec);
    
    // 配置参数 (注意：有些摄像头是H265，像素格式需要转换，这里简化处理)
    pCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    pCtx->width = pFrame->width;
    pCtx->height = pFrame->height;
    pCtx->time_base = {1, 25};

    if (avcodec_open2(pCtx, pCodec, nullptr) < 0) return;

    AVPacket* pkt = av_packet_alloc();
    if (avcodec_send_frame(pCtx, pFrame) == 0) {
        if (avcodec_receive_packet(pCtx, pkt) == 0) {
            FILE* f = fopen(filename.c_str(), "wb");
            fwrite(pkt->data, 1, pkt->size, f);
            fclose(f);
        }
    }

    av_packet_free(&pkt);
    avcodec_free_context(&pCtx);
}