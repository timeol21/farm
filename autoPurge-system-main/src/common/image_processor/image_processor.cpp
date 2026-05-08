#include "common/image_processor/image_processor.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include <fstream>
// #include "base64.h"
#include "/home/ztl/workspace/Framework/lib/rknn/3rdparty/jpeg_turbo/include/turbojpeg.h"
bool ImageProcessor::avframeToRGB(AVFrame* frame, int width, int height, image_buffer_t* out_image) {
    if (!frame || !out_image || width <= 0 || height <= 0) {
        std::cerr << "Error: Invalid parameters" << std::endl;
        return -1;
    }

    SwsContext* sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         width, height, AV_PIX_FMT_RGB24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        std::cerr << "Error: Failed to create SwsContext!" << std::endl;
        return -1;
    }

    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        std::cerr << "Error: Failed to allocate RGB AVFrame!" << std::endl;
        sws_freeContext(sws_ctx);
        return -1;
    }

    rgb_frame->width = width;
    rgb_frame->height = height;
    rgb_frame->format = AV_PIX_FMT_RGB24;

    if (av_frame_get_buffer(rgb_frame, 0) < 0) {
        std::cerr << "Error: Failed to allocate buffer for RGB AVFrame!" << std::endl;
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_ctx);
        return -1;
    }

    if (sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                  rgb_frame->data, rgb_frame->linesize) <= 0) {
        std::cerr << "Error: sws_scale failed!" << std::endl;
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_ctx);
        return -1;
    }

    // ✅ 拷贝到输出结构体
    out_image->width = width;
    out_image->height = height;
    out_image->format = IMAGE_FORMAT_RGB888;
    out_image->size = width * height * 3;
    out_image->virt_addr = (unsigned char*)malloc(out_image->size);
    memcpy(out_image->virt_addr, rgb_frame->data[0], out_image->size);

    av_frame_free(&rgb_frame);
    sws_freeContext(sws_ctx);
    return 0;
}

void ImageProcessor::drawDetections(image_buffer_t* rgb_image,object_detect_result_list& results) {
    if(!rgb_image){
        std::cerr<<"Error: RGB image is null!"<<std::endl;
        return;
    }
    for(int i=0; i<results.count; i++){
        object_detect_result *det = &results.results[i];
        if(!det){
            continue;
        }
        // 1. 提取目标边界框坐标
        int x1 = det->box.left;    // 左上角 x
        int y1 = det->box.top;     // 左上角 y
        int x2 = det->box.right;   // 右下角 x
        int y2 = det->box.bottom;  // 右下角 y

        // 确保坐标在图像范围内（避免越界绘图）
        x1 = std::max(0, std::min(x1, rgb_image->width - 1));
        y1 = std::max(0, std::min(y1, rgb_image->height - 1));
        x2 = std::max(x1 + 1, std::min(x2, rgb_image->width - 1));
        y2 = std::max(y1 + 1, std::min(y2, rgb_image->height - 1));

        // 2. 绘制边界框（蓝色，线宽 3 像素）
        // 注意：项目的 draw_rectangle 函数参数可能为（图像，x，y，宽，高，颜色，线宽）
        int rect_width = x2 - x1;   // 矩形宽度
        int rect_height = y2 - y1;  // 矩形高度
        draw_rectangle(rgb_image, x1, y1, rect_width, rect_height, COLOR_BLUE, 3);

        // 3. 生成标签文本（类别 + 置信度）
        char label[128];
        sprintf(label, "%s %.1f%%", 
                coco_cls_to_name(det->cls_id),  // 类别名称（如 "person"）
                det->prop * 100);               // 置信度（转换为百分比）

        // 4. 绘制标签文本（红色，字体大小 16，位于框上方）
        // 调整文本 y 坐标，避免超出图像顶部
        int text_y = (y1 - 20) > 0 ? (y1 - 20) : 20;  // 框上方 20 像素，或顶部 20 像素
        draw_text(rgb_image, label, x1, text_y, COLOR_RED, 16);
    }
}

bool ImageProcessor::compressToJpeg(const image_buffer_t* rgb_image,std::vector<unsigned char>& outJpeg,int quality)
{


    // JPEG压缩参数
    int ret;
    int jpegSubsamp = TJSAMP_422;
    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    int flags = 0;

    const unsigned char* data = (const unsigned char*)rgb_image->virt_addr;
    int width = rgb_image->width;
    int height = rgb_image->height;
    int pixelFormat = TJPF_RGB;

    tjhandle handle = tjInitCompress();

    // 检查像素格式
    if (rgb_image->format != IMAGE_FORMAT_RGB888) {
        std::cerr << "Unsupported pixel format: " << rgb_image->format << std::endl;
        tjDestroy(handle);
        return false;
    }

    // 压缩成JPEG
    ret = tjCompress2(handle, data, width, 0, height, pixelFormat,
                      &jpegBuf, &jpegSize, jpegSubsamp, quality, flags);
    if (ret != 0 || jpegBuf == nullptr) {
        std::cerr << "JPEG compression failed!" << std::endl;
        tjDestroy(handle);
        return false;
    }

    outJpeg.assign(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);
    tjDestroy(handle);
    return true;
}

std::string ImageProcessor::jpegToBase64(const std::vector<unsigned char>& jpegData)
{
    //  if (jpegData.empty()) {
    //     return "";
    // }

    // return base64_encode(jpegData.data(), jpegData.size());
    return "";
}

bool ImageProcessor::saveJpegToFile(const std::vector<unsigned char>& jpegData, const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }   
    
    file.write(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());
    file.close();
    return true;
}

static bool compressFrameToJpeg(const std::shared_ptr<AVFrame>& frame,std::vector<unsigned char>& outJpeg,int quality){
    // if (!frame) {
    //     std::cerr << "Error: AVFrame is null" << std::endl;
    //     return false;
    // }

    // tjhandle handle = tjInitCompress();
    // if (!handle) {
    //     std::cerr << "Error: tjInitCompress failed" << std::endl;
    //     return false;
    // }

    // // 从 AVFrame 获取宽高
    // int width = frame->width;
    // int height = frame->height;

    // // 标准视频帧格式：YUV420P
    // // int pixelFormat = TJPF_YUV420;
    // int jpegSubsamp = TJSAMP_420;
    // int flags = TJFLAG_FASTDCT;

    // unsigned char* jpegBuf = nullptr;
    // unsigned long jpegSize = 0;

    // // 执行压缩
    // int ret = tjCompress2(
    //     handle,
    //     frame->data[0],
    //     width,
    //     frame->linesize[0],
    //     height,
    //     // pixelFormat,
    //     &jpegBuf,
    //     &jpegSize,
    //     jpegSubsamp,
    //     quality,
    //     flags
    // );

    // if (ret != 0 || jpegBuf == nullptr) {
    //     std::cerr << "Error: JPEG compression failed" << std::endl;
    //     tjDestroy(handle);
    //     return false;
    // }

    // // 输出到 vector
    // outJpeg.assign(jpegBuf, jpegBuf + jpegSize);

    // // 释放资源
    // tjFree(jpegBuf);
    // tjDestroy(handle);

    return true;
}