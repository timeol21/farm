#pragma once
#include "data_layer/ai/ai_dao.h"
#include <atomic>
#include <thread>
class IDetectionService{
public:
    virtual ~IDetectionService() = default;

    // 初始化检测服务，加载模型等
    virtual  bool initialize() = 0;

    // 对单帧进行检测
    virtual void detectFrame() = 0;

    // 对视频缓冲区的多帧进行检测
    virtual void detectBufferedFrames() = 0;

     // 重新加载模型
    virtual bool reloadModel() = 0;

    //得到检测结果
    virtual void getDetectionResultsByFrame() = 0;
};


class DetectionService : public IDetectionService{
    DetectionService(IModuleDao& dao);
    ~DetectionService();

    bool initialize() override;

    void detectFrame() override;

    void detectBufferedFrames() override;

    bool reloadModel() override;

    void getDetectionResultsByFrame() override;
    
private:
    void detectionLoop();            // 循环检测函数



private:
    IModuleDao& ModuleDa; //得到模型，目标集，path，


    // FrameBuffer m_frameBuffer;//缓冲对象，从这里面去拿取视频帧

    std::atomic<bool> m_running;    // 检测线程运行标记
    std::thread m_detectionThread;  // 后台检测线程

};

