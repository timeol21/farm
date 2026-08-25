#pragma once
#include "HCNetSDK.h"//sdk
#include "INVR.h"
#include "video_data_object.h"
#include <cstring>
#include <map>
#include <iostream>
#include <atomic>
#include <thread>
#include <filesystem> 
#include <unordered_map>
#include <memory>
#include <iomanip> 
#include <unistd.h>
#include "FileUtils.h"
#include "logger.h"

extern "C" {
#include <libavutil/time.h>
}
/**
 * @brief 海康NVR设备具体实现类
 * 核心职责：实现IDevice接口，
 */
class HKVDevice : public INVR {
public:
    HKVDevice() = default;

    ~HKVDevice() = default;

    bool initSDK() override;
    bool deinitSDK() override;

    bool login(const std::string& ip, short port,
               const std::string& user, const std::string& password) override;

    bool logout() override;

    bool start(int channel,Camera* camera) override;

    bool stop(int channel,Camera* camera)override;

    bool queryRecordFiles(int channel, std::string starttime, std::string endtime, VideoFileInfos& outFiles) override;
    
    bool downloadRecordFile(DownloadVideoFile& info) override;

    // bool forceIFrame(int channelId) override; // channel 是否已经 RealPlay

    // bool getDeviceInfo(DeviceInfo& info) override;

    bool getSDKStatus() override {return sdkInited_;}

    bool getNVRStatus() override {return nvrInited_;}

    int returnDownloadprogress() override;

    DownloadFile getDownloadResult() override;

    bool isDownloadFinished()override;

    bool registerPassage(std::vector<CameraConfig> cameras) override;

    bool ensureRecordPlan(int channel)override;

    bool ensureRecycleStorage()override;


private:
    void pullRealPlayLoop(int channel);
    static void CALLBACK onHKFrameCallback(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX *pstruPackInfo, void *pUser) ; // SDK回调函数 
    Camera* findCameraByChannel(int channel);//根据这个channel来进行相关的

    void downloadWorker(DownloadVideoFile info);

    bool registerSingleChannel(const std::string& ipcIp,int port,const std::string& user,const std::string& pwd);

private:
    int userId_ = -1;
    bool sdkInited_ = false;   
    bool nvrInited_ = false;
    struct ChannelContext {
        int realHandle = -1;
        Camera* camera = nullptr;
        std::atomic_bool running{false};
        std::thread pullThread;
        
    };
    std::map<int, ChannelContext> channels_;//SDK 通道句柄 + 通道运行状态 运行状态
    std::mutex ctxMutex_;

    int downloadId_ = -1;
    std::thread downloadThread_;
    std::mutex download_;
    std::atomic<bool> runningDownload_{false};
    std::mutex downloadMutex_;

    std::atomic<int>  downloadProgress{0};
    std::mutex downloadProgress_;

    DownloadFile downloadResult_;
};

class Utils {
public:
    /**
     * @brief 将yyyy-MM-dd HH:mm:ss格式字符串转为SDK时间结构体
     * @param timeStr 时间字符串（如"2025-01-01 08:00:00"）
     * @return NET_DVR_TIME SDK时间结构体
     */
    static NET_DVR_TIME getNvrTime(const std::string& timeStr) {
        NET_DVR_TIME nvrTime = {0};
        std::tm tm = {0};
        std::istringstream iss(timeStr);
        // 解析yyyy-MM-dd HH:mm:ss
        iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (iss.fail()) {
            throw std::invalid_argument("时间格式错误，需为yyyy-MM-dd HH:mm:ss");
        }

        nvrTime.dwYear = tm.tm_year + 1900;    // tm_year是从1900开始的差值
        nvrTime.dwMonth = tm.tm_mon + 1;       // tm_mon是0-11，SDK是1-12
        nvrTime.dwDay = tm.tm_mday;
        nvrTime.dwHour = tm.tm_hour;
        nvrTime.dwMinute = tm.tm_min;
        nvrTime.dwSecond = tm.tm_sec;

        return nvrTime;
    }

    /**
     * @brief 将SDK时间结构体转为yyyy-MM-dd HH:mm:ss字符串
     * @param sdkTime SDK时间结构体
     * @return 格式化时间字符串
     */
    static std::string sdkTimeToStr(const NET_DVR_TIME& sdkTime) {
        std::tm tm = {0};
        tm.tm_year = sdkTime.dwYear - 1900;
        tm.tm_mon = sdkTime.dwMonth - 1;
        tm.tm_mday = sdkTime.dwDay;
        tm.tm_hour = sdkTime.dwHour;
        tm.tm_min = sdkTime.dwMinute;
        tm.tm_sec = sdkTime.dwSecond;

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    static bool parseToBytes(const std::string& input, size_t& outBytes);

};