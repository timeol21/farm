#pragma once

#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include "data_layer/device/frame_data_object.h"
#include <vector>
class FrameBuffer{
public:
    // 显式构造函数，初始化队列容量
    explicit FrameBuffer(size_t cameraCapacity = 14) : cameraCapacity_(cameraCapacity) {}

    ~FrameBuffer();

    bool registerCamera(const CameraChannelInfo& info);

    void updateFrame(const CameraChannelInfo& info, std::shared_ptr<FrameData> frame);

    

    bool getShapshout(const std::string& cameraId, CameraFrameSnapshot& out) const;

    std::vector<CameraFrameSnapshot> getAllLastFrames() const;

    void clearFrames();

    // CameraFrameResult getLastFrameJPEG(const std::string& cameraId) const; //CameraFrameResult

    // CameraFrameResult getLastFrameBase64(const std::string& cameraId) const;
private:
    size_t cameraCapacity_;                  // 队列最大容量
    mutable std::mutex mutex_;         // 互斥锁（mutable 允许 const 成员函数加锁）
    std::condition_variable cond_;      // 条件变量
    // key = cameraId
    std::unordered_map<std::string, CameraFrameSnapshot> frameMap;
};

