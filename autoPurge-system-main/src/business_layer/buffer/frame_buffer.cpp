#include "business_layer/buffer/frame_buffer.h"
#include <iostream>
#include "common/log/log_manager.h"
#include "business_layer/lobby/lobby_object.h"
#include "common/image_processor/image_processor.h"

FrameBuffer::~FrameBuffer() {
    clearFrames();
}

bool FrameBuffer::registerCamera(const CameraChannelInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 超过最大容量，注册失败
    if (cameraCapacity_ > 0 && frameMap.size() >= cameraCapacity_) {
        return false;
    }

    // 摄像头已存在，直接返回成功
    if (frameMap.find(info.deviceId) != frameMap.end()) {
        return true;
    }

    // 新增摄像头，初始化快照
    frameMap[info.deviceId] = CameraFrameSnapshot{info, nullptr, 0};
    return true;
}

void FrameBuffer::updateFrame(const CameraChannelInfo& info, std::shared_ptr<FrameData> frame) {
    if (!frame) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t ts = frame->lastKeyFrameTime;

    auto it = frameMap.find(info.deviceId);
    if (it == frameMap.end()) {
        // 自动注册新摄像头
        if (cameraCapacity_ > 0 && frameMap.size() >= cameraCapacity_) {
            std::cout << "[FrameBuffer::updateFrame] capacity full, cameraId=" << info.deviceId << std::endl;
            return;
        }
        frameMap[info.deviceId] = CameraFrameSnapshot{info, frame, ts};
    } else {
        // 更新已有摄像头的帧数据和信息
        it->second.cameraInfo = info;
        it->second.frame = frame;
        it->second.timestamp = ts;
    }
}

bool FrameBuffer::getShapshout(const std::string& cameraId, CameraFrameSnapshot& out) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = frameMap.find(cameraId);
    if (it == frameMap.end()) {
        return false;
    }

    out = it->second;
    return true;
}

std::vector<CameraFrameSnapshot> FrameBuffer::getAllLastFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<CameraFrameSnapshot> result;
    result.reserve(frameMap.size());

    for (const auto& pair : frameMap) {
        const auto& snapshot = pair.second;
        if (snapshot.frame) {
            result.push_back(snapshot);
        }
    }

    return result;
}

void FrameBuffer::clearFrames() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : frameMap) {
        auto& snapshot = pair.second;
        snapshot.frame.reset();    // 释放帧数据
        snapshot.timestamp = 0;    // 重置时间戳
    }
}






// CameraFrameResult FrameBuffer::getLastFrameJPEG(const std::string& cameraId) const{
//     // std::lock_guard<std::mutex> lock(mutex_);

//     image_buffer_t rgb_image{};
//     std::vector<unsigned char> outJpeg;
    
//     auto it = frameMap.find(cameraId);
//     if (it == frameMap.end()) { //没有找到
//         std::cerr << "Error: cameraId not found: " << cameraId << std::endl;
//         return CameraFrameResult();
//     }

//     if (!it->second.frame->frame) {//帧不存在
//         std::cerr << "Error: frame is null for cameraId: " << cameraId << std::endl;
//         return CameraFrameResult();
//     }

//     int ret = ImageProcessor::avframeToRGB(it->second.frame->frame.get(), 640, 640, &rgb_image);
//     if (ret != 0) {
//         std::cerr << "Error: Failed to convert AVFrame to RGB image!" << std::endl;
//         return CameraFrameResult();
//     }

//     ret = ImageProcessor::compressToJpeg(&rgb_image, outJpeg);
//     if (ret != 0 || outJpeg.empty()) {
//         std::cerr << "Error: Failed to compress RGB image to JPEG!" << std::endl;
//         return CameraFrameResult();
//     }

//     free(rgb_image.virt_addr);

//     return CameraFrameResult(
//         cameraId,
//         std::move(outJpeg),
//         "",
//         it->second.timestamp
//     );
// }

// CameraFrameResult FrameBuffer::getLastFrameBase64(const std::string& cameraId) const{
//     // std::lock_guard<std::mutex> lock(mutex_);

//     image_buffer_t rgb_image{};
//     std::vector<unsigned char> outJpeg;
    
//     auto it = frameMap.find(cameraId);
//     if (it == frameMap.end()) { //没有找到
//         std::cerr << "Error: cameraId not found: " << cameraId << std::endl;
//         return CameraFrameResult();
//     }

//     if (!it->second.frame->frame) {//帧不存在
//         std::cerr << "Error: frame is null for cameraId: " << cameraId << std::endl;
//         return CameraFrameResult();
//     }

//     int ret = ImageProcessor::avframeToRGB(it->second.frame->frame.get(), 640, 640, &rgb_image);
//     if (ret != 0) {
//         std::cerr << "Error: Failed to convert AVFrame to RGB image!" << std::endl;
//         return CameraFrameResult();
//     }

//     ret = ImageProcessor::compressToJpeg(&rgb_image, outJpeg);
//     if (ret != 0 || outJpeg.empty()) {
//         std::cerr << "Error: Failed to compress RGB image to JPEG!" << std::endl;
//         return CameraFrameResult();
//     }

//     std::string imageBase64 = ImageProcessor::jpegToBase64(outJpeg);
//     free(rgb_image.virt_addr);

//     return CameraFrameResult(
//         cameraId,
//         {},
//         std::move(imageBase64),
//         it->second.timestamp
//     );

// }
