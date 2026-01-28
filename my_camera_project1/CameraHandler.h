#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/avutil.h>
}

class CameraHandler {
public:
    /**
     * @param rtspUrl 摄像头的RTSP地址
     * @param name 实例名称（用于区分生成的文件名，如 "cam1"）
     * @param jpgInterval 抓拍间隔（秒），默认5秒
     */
    CameraHandler(const std::string& rtspUrl, const std::string& name, int jpgInterval = 5);
    
    ~CameraHandler();

    // 启动录制与抓拍双线程
    void start();
    
    // 停止所有任务并释放资源
    void stop();

private:
    // 录制线程：负责原始流打包成MP4，支持自动切片与边录边看
    void recordLoop();
    
    // 抓拍线程：负责解码视频流，定时生成最新JPG图片
    void snapLoop();

    // 辅助函数：将解码后的AVFrame编码为JPEG文件
    bool saveFrameAsJpeg(AVFrame* frame, const std::string& path);

private:
    // 配置参数
    std::string rtspUrl_;
    std::string name_;
    int jpgInterval_;

    // 状态控制
    std::atomic<bool> running_{false};
    
    // 线程对象
    std::thread recordThread_;
    std::thread snapThread_;

    // 资源锁（如果后续需要多线程访问共享资源时使用）
    std::mutex mtx_;
};

#endif // CAMERA_HANDLER_H