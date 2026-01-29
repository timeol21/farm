extern "C" {               //只能解码成功，不能展示具体的图片
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <iostream>

int main() {
    const char* url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";

    // 1 & 2. 分配上下文并打开 RTSP 流 (文档步骤 1-2)
    AVFormatContext* pFormatCtx = avformat_alloc_context();
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0); // 强制TCP，保证数据完整

    if (avformat_open_input(&pFormatCtx, url, nullptr, &options) < 0) {
        std::cerr << "无法建立连接" << std::endl;
        return -1;
    }

    // 3 & 4. 获取流信息并找到视频流索引 (文档步骤 3-4)
    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) return -1;
    
    int videoStreamIdx = -1;
    for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    // 5. 获取视频流解码器 (文档步骤 5)
    // 自动根据流里的信息（如H265）找到对应的解码器模块
    AVCodecParameters* pCodecPar = pFormatCtx->streams[videoStreamIdx]->codecpar;
    AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);

    // 6. 分配解码器上下文并复制参数 (文档步骤 6)
    // 这一步相当于告诉解码器：画面的宽、高、编码格式是多少
    AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pCodecPar);

    // 7. 打开解码器 (文档步骤 7)
    // 到这一步，“手术室”准备完毕，解码器可以随时开工了
    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        std::cerr << "无法打开解码器" << std::endl;
        return -1;
    }

    std::cout << "解码器初始化成功，准备解析画面..." << std::endl;
    
    // 后续步骤（8-10）将在下面讲解...
    // 准备“快递盒”和“画板”
    AVPacket* pPacket = av_packet_alloc();
    AVFrame* pFrame = av_frame_alloc();

    int frameCount = 0;
    // 8. 从流中读取一帧数据 (文档步骤 8)
    while (av_read_frame(pFormatCtx, pPacket) >= 0) {
        if (pPacket->stream_index == videoStreamIdx) {
            
            // 9. 发送数据包到解码器 (文档步骤 9)
            // 相当于把“快递盒”塞进“解压机器”
            int send_ret = avcodec_send_packet(pCodecCtx, pPacket);
            
            while (send_ret >= 0) {
                // 10. 接收解码后的帧 (文档步骤 10)
                // 相当于从机器里吐出一张“画”
                int receive_ret = avcodec_receive_frame(pCodecCtx, pFrame);
                
                if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
                    break; 
                } else if (receive_ret < 0) {
                    std::cerr << "解码出错" << std::endl;
                    break;
                }

                // --- 成功拿到一帧画面 pFrame ---
                frameCount++;
                if (frameCount % 30 == 0) { // 每30帧打印一次，防止刷屏
                    std::cout << "成功解码第 " << frameCount << " 帧，分辨率: " 
                              << pFrame->width << "x" << pFrame->height << std::endl;
                    
                    // TODO: 以后你的“物品识别”算法就在这里处理 pFrame！
                }
            }
        }
        av_packet_unref(pPacket); // 拆完一个快递，清理一个盒子
    }
    return 0;
}

/*
g++ main_decode.cpp -o decode_app -lavformat -lavcodec -lavutil
*/