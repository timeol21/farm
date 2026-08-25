#pragma once

#include <memory>
#include "config_parser.h"
#include "video_data_object.h"
class INVR;
//NVR工厂类：
class NVRFactory{
public:
    static std::unique_ptr<INVR> createNVR(const NVRConfig& nvrConfig);
};

class INVR {
public:
    virtual ~INVR() = default;

    virtual bool initSDK() = 0;
    virtual bool deinitSDK() = 0;

    virtual bool login(const std::string& ip, short port,
               const std::string& user, const std::string& password) = 0;

    virtual bool logout() = 0;

    virtual bool start(int channel,Camera* camera) = 0;

    virtual bool stop(int channel,Camera* camera) = 0;

    // virtual bool forceIFrame(int channelId) = 0; // channel 是否已经 RealPlay

    // virtual bool getDeviceInfo(DeviceInfo& out) = 0;

    virtual bool getSDKStatus() = 0;

    virtual bool getNVRStatus() = 0;

    // ===== 新增：录像下载 =====
    virtual bool queryRecordFiles(int channel, std::string starttime, std::string endtime, VideoFileInfos& outFiles) = 0;

    virtual bool downloadRecordFile(DownloadVideoFile& info) = 0;

    virtual int returnDownloadprogress() = 0;

    virtual DownloadFile getDownloadResult() = 0;

    virtual bool isDownloadFinished() = 0;

    virtual bool registerPassage(std::vector<CameraConfig> cameras) = 0;

    virtual bool ensureRecordPlan(int channel) = 0;

    virtual bool ensureRecycleStorage() = 0;
};