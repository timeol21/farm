#include <iostream>
#include <string>

// 必须包含 FFmpeg 库的头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

// 声明保存 JPG 的函数，方便在 main 中调用
void save_as_jpg(AVFrame* pFrame, int index);

int main() {
    // 摄像头的 RTSP 地址
    const char* url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";

    // 1. 初始化网络环境 (FFmpeg 库要求)
    avformat_network_init();

    // 1. 分配格式上下文 (文档步骤 1)
    AVFormatContext* pFormatCtx = avformat_alloc_context();

    // 2. 打开 RTSP 流 (文档步骤 2)
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0); // 强制使用 TCP
    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) {
        std::cerr << "无法建立连接" << std::endl;
        return -1;
    }

    // 3. 获取流信息 (文档步骤 3)
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        return -1;
    }

    // 4. 查找视频流索引 (文档步骤 4)
    int videoIdx = -1;
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = i;
            break;
        }
    }
    if (videoIdx == -1) return -1;

    // 5. 获取视频流解码器 (文档步骤 5)
    AVCodecParameters* pParams = pFormatCtx->streams[videoIdx]->codecpar;
    AVCodec* pDecoder = avcodec_find_decoder(pParams->codec_id);
    if (!pDecoder) return -1;

    // 6. 分配解码器上下文并复制参数 (文档步骤 6)
    AVCodecContext* pDecCtx = avcodec_alloc_context3(pDecoder);
    avcodec_parameters_to_context(pDecCtx, pParams);

    // 7. 打开解码器 (文档步骤 7)
    if (avcodec_open2(pDecCtx, pDecoder, nullptr) < 0) {
        std::cerr << "无法打开解码器" << std::endl;
        return -1;
    }

    // 准备 AVPacket（快递盒）和 AVFrame（解出的画）
    AVPacket* pPkt = av_packet_alloc();
    AVFrame* pFrame = av_frame_alloc();

    std::cout << "正在拉流并截取画面，请稍候..." << std::endl;

    int frame_count = 0;
    // 8. 循环读取每一帧数据包 (文档步骤 8)
    while (av_read_frame(pFormatCtx, pPkt) >= 0) {
        if (pPkt->stream_index == videoIdx) {
            // 9. 发送数据包到解码器 (文档步骤 9)
            if (avcodec_send_packet(pDecCtx, pPkt) == 0) {
                // 10. 接收解码后的帧 (文档步骤 10)
                while (avcodec_receive_frame(pDecCtx, pFrame) == 0) {
                    frame_count++;
                    // 为了画面稳定，我们存下第 50 帧画面
                    if (frame_count == 50) {
                        save_as_jpg(pFrame, frame_count);
                        std::cout << "保存成功！截图已保存为 capture.jpg" << std::endl;
                        goto end; // 任务完成，跳出循环
                    }
                }
            }
        }
        av_packet_unref(pPkt);
    }

end:
    // 清理资源
    av_frame_free(&pFrame);
    av_packet_free(&pPkt);
    avcodec_free_context(&pDecCtx);
    avformat_close_input(&pFormatCtx);
    avformat_network_deinit();

    return 0;
}

// 编码步骤实现：将 AVFrame 保存为 JPG 格式 (严格对应文档编码步骤)
void save_as_jpg(AVFrame* pFrame, int index) {
    // 1. 找到 MJPEG 编码器
    AVCodec* pCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!pCodec) return;

    // 2. 配置编码器上下文
    AVCodecContext* pCtx = avcodec_alloc_context3(pCodec);
    pCtx->pix_fmt = AV_PIX_FMT_YUVJ420P; // JPG 常用像素格式
    pCtx->width = pFrame->width;
    pCtx->height = pFrame->height;
    pCtx->time_base = {1, 25};

    // 3. 打开编码器
    if (avcodec_open2(pCtx, pCodec, nullptr) < 0) return;

    // 4. 发送原始帧到编码器
    avcodec_send_frame(pCtx, pFrame);

    // 5. 接收编码后的 JPG 数据包
    AVPacket* pPkt = av_packet_alloc();
    if (avcodec_receive_packet(pCtx, pPkt) == 0) {
        // 6. 写入硬盘
        FILE* f = fopen("capture.jpg", "wb");
        if (f) {
            fwrite(pPkt->data, 1, pPkt->size, f);
            fclose(f);
        }
    }

    // 清理临时资源
    av_packet_free(&pPkt);
    avcodec_free_context(&pCtx);
}

/*
g++ main_3.cpp -o capture_app -lavformat -lavcodec -lavutil
*/


/*
（main_3.cpp）做了以下三件大事：

拉流（Pulling）：建立 RTSP 连接，把压缩的数据包（AVPacket）取回本地。

解码（Decoding）：把压缩包拆开，还原成了原始的、可见的像素帧（AVFrame）。这是最难的一步，也是做“物品识别”的前提。

编码（Encoding）：把这一帧像素重新包装成了人类能直接打开看的 .jpg 文件。
*/