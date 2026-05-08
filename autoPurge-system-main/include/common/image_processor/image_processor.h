#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <string>
#include "common/yolo8/yolov8.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
extern "C" {
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

}
class ImageProcessor {
public:
    static bool avframeToRGB(AVFrame* frame, int width, int height, image_buffer_t* out_image);
    static void drawDetections(image_buffer_t* rgb_image,object_detect_result_list& results);
    static bool compressToJpeg(const image_buffer_t* rgb_image,std::vector<unsigned char>& outJpeg,int quality = 95);

    static bool compressFrameToJpeg(const std::shared_ptr<AVFrame>& frame,std::vector<unsigned char>& outJpeg,int quality);

    static std::string jpegToBase64(const std::vector<unsigned char>& jpegData);
    
    static bool saveJpegToFile(const std::vector<unsigned char>& jpegData, const std::string& filename);
};

#endif // IMAGE_PROCESSOR_H