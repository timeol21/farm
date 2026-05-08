#pragma once
#include <functional>
#include "business_layer/buffer/frame_buffer.h"
#include "business_layer/detection/detection_service_object.h"
#include "data_layer/device/frame_data_object.h"
#include "data_layer/detection/model_dao.h"
#include <queue>
class IDetector{
public:    
    virtual ~IDetector() = default;
    
    virtual bool initialize() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void setCallback(std::function<void(const DetectionResult&)> cb) = 0;


};

class AIDetector : public IDetector {
public:
    AIDetector();
    ~AIDetector();

    bool initialize()override;
    void start() override;
    void stop() override;

    void setCallback(std::function<void(const DetectionResult&)> cb) override;

    void reloadModel();

private:

    // 转码线程
    void convertLoop();

    // 推理线程
    void inferLoop();

    
    // bool shouldDetect(const KeyFrame& frame);

    // void preprocessFrame(const KeyFrame& snapshot, image_buffer_t& image);

    // object_detect_result_list infer(image_buffer_t& image);

    // void handleDetectResult(
    //     const KeyFrame& snapshot,
    //     const object_detect_result_list& results
    // );

    // void releaseImageBuffer(image_buffer_t& img);

private:    
    // std::unique_ptr<IModelDao> modelDao_;

    // FrameBuffer& frameBuffer;

    // // 队列（你说的那个）
    // std::queue<Frame> frameQueue;

    std::function<void(const DetectionResult&)> callback;
};



class RadarDetector : public IDetector {

public:
    RadarDetector();
    ~RadarDetector();
    bool initialize()override;
    void start() override;
    void stop() override;

    void setCallback(std::function<void(const DetectionResult&)> cb) override;

private:
    // 雷达数据缓存
    // RadarBuffer buffer;

    // 对比逻辑
    void scanLoop();

    std::function<void(const DetectionResult&)> callback;
};
