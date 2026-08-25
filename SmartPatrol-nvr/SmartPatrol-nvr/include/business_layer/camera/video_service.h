#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include "hikvision/hikDevice.h"
#include "video_data_object.h"
#include "INVR.h"

class PreviewStream;
class PreviewFrame;
class VideoFrames;

class IVideoService {
public:
    virtual ~IVideoService() = default;

    virtual void start() = 0;

    virtual void stop() = 0;

    virtual bool getDeviceStatus(VideoDerviceStatusInfo& videoDerviceStatusInfo) = 0;//获取视频的设备状态（nvr 加上全部摄像头） //上面主动上传上去
    
    virtual bool viewCameraPreviewStream(const PreviewStream& in,PreviewFrame& out) = 0; // 获取单个通道的最新预览帧

    virtual bool getAllLastKeyFrames(VideoFrames& out) = 0;
    
    virtual bool queryRecordFiles(std::string cameraId,std::string startTime, std::string endTime,VideoFiles& outFiles) = 0;

    virtual bool downloadRecordFile(DownloadVideoFile& in, DownloadReadyFile& out) = 0;

    virtual int getDownloadProgress() = 0;
};

class VideoService : public IVideoService {
public:   
    ~VideoService()override;
    VideoService();

    void start() override;

    void stop() override;
    
    bool getDeviceStatus(VideoDerviceStatusInfo& out)override;//获取视频的设备状态（nvr 加上全部摄像头） //上面主动上传上去
    
    bool viewCameraPreviewStream(const PreviewStream& in,PreviewFrame& out) override; // 获取单个通道的最新预览帧

    bool getAllLastKeyFrames(VideoFrames& out) override;

    bool queryRecordFiles(std::string cameraId,std::string startTime, std::string endTime,VideoFiles& outFiles)override;

    bool downloadRecordFile(DownloadVideoFile& in, DownloadReadyFile& out) override;

    int getDownloadProgress() override;

private:
    bool addCamera(const CameraInfo& info);   //可能就是有业务需求的要移动走后的摄像头，重新添加到里面
    bool removeCamera(const CameraInfo& info); //可能就是有业务需求的要移动走
    bool registerDevices();// 把摄像头都注册到这个map表里面，而且初始化SDK，还把NVR进行登陆
    void startAllStreams();//启动所有的通道预览流
    std::string formatFileSize(uint64_t bytes);
    
private:
    std::unique_ptr<INVR> nvr_;
    std::map<std::string, std::unique_ptr<Camera>> cameras_;  //摄像头的在线表（通道信息 + 状态 + 缓存帧）”）  通道号来进行
    std::mutex mutex_; 
    std::atomic_bool running_;
};








