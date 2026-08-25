#pragma once


#include <cstdint>
#include <vector>
#include <mutex>
#include <string>
#include <memory>
#include <iostream>
#include <map>
#include <ctime>       // time_t、tm 结构体
#include <sstream>     // std::ostringstream
#include <iomanip>     // std::put_time
#include <stdexcept>

extern "C" {
#include <libavutil/frame.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libavutil/error.h>
#include <libavutil/avutil.h>
}

//录像的文件信息
struct VideoFileInfo {
    std::string starttime;  // 开始时间 yyyy-MM-dd HH:mm:ss
    std::string endtime;    // 结束时间 yyyy-MM-dd HH:mm:ss
    std::string filename;   // 文件名
    unsigned int filesize;  // 文件大小（字节）
};

struct VideoFileInfos {
    bool isSuccess;         // 查询是否成功
    std::string errorMsg;   // 错误信息（失败时填充）
    std::vector<VideoFileInfo> fileList; // 录像文件列表
};


struct DownloadFile{
    std::string fileName;    // 文件名
    int downloadResult;     // 下载结果（1:完成，0进行中 ,-1:失败） //std::atomic<int> downloadResult{0};
    std::string localPath; //下载地址
    std::string processError;

    // ===== 工具函数 =====
    bool isFinished() const {
        if(downloadResult == 0) return true;
        else return false;
    }

    bool isSuccess() const {
        return downloadResult == 1;
    }
};




//dao层对象

enum CameraStatus{ 
    ONLINE = 0,//在线
    RUNNING = 1,//运行
    OFFLINE = -1//离线
};

struct CameraInfo{//给底层的Camera用的
    std::string  cameraId;
    std::string  nvrId;
    int channel;
    std::string name;
    CameraStatus status; 
    std::string ip;
};

struct NVRInfo{
    std::string brank;
    std::string ip;
    std::string username;
    std::string password;
    short port;
};

struct FrameData {
    std::shared_ptr<AVFrame> frame;  // 解码后的 RGB/YUV
    int width = 0;
    int height = 0;
    uint64_t lastKeyFrameTime = 0;
};
//业务对象

class DownloadVideoFile {
public:
    DownloadVideoFile() = default;

    DownloadVideoFile(const std::string& cameraId,
                  const std::string& nvrId,
                  const std::string& fileName,
                  const std::string& startTime,
                  const std::string& fileSize = "")
    : cameraId_(cameraId),
      nvrId_(nvrId),
      fileName_(fileName),
      fileSize_(fileSize) {
        setStartTime(startTime);
      }

    // ===== Getter =====
    const std::string& cameraId()  const { return cameraId_; }
    const std::string& nvrId()     const { return nvrId_; }
    const std::string& fileName()  const { return fileName_; }
    const std::string& startTime() const { return startTime_; }
    const std::string& fileSize()  const { return fileSize_; }

    void setStartTime(const std::string& startTime) {
        startTime_ = startTime;
        // 替换空格为下划线
        size_t spacePos = startTime_.find(' ');
        if (spacePos != std::string::npos) {
            startTime_[spacePos] = '_';
        }
    }

private:
    // 👇 实际成员（推荐加 _）
    std::string cameraId_;
    std::string nvrId_;
    std::string fileName_;   // 
    std::string fileSize_; 
    std::string startTime_;  // yyyy-MM-dd HH:mm:ss 或时间戳字符串
};


class DownloadReadyFile {
public:
    // 无参构造函数
    DownloadReadyFile() 
        : downloadResult(0)  // 默认下载状态为"进行中"
    {}

    // 带参构造函数（方便一次性初始化所有成员）
    DownloadReadyFile(const std::string& cameraId, 
                      const std::string& nvrId, 
                      const std::string& fileName, 
                      const std::string& fileSize, 
                      int downloadResult, 
                      const std::string& localPath, 
                      const std::string& processError)
        : cameraId(cameraId),
          nvrId(nvrId),
          fileName(fileName),
          fileSize(fileSize),
          downloadResult(downloadResult),
          localPath(localPath),
          processError(processError)
    {}
    // -------------------------- cameraId 的 get/set 方法 --------------------------
    std::string getCameraId() const {
        return cameraId;
    }
    void setCameraId(const std::string& cameraId) {
        this->cameraId = cameraId;
    }
    // -------------------------- nvrId 的 get/set 方法 --------------------------
    std::string getNvrId() const {
        return nvrId;
    }

    void setNvrId(const std::string& nvrId) {
        this->nvrId = nvrId;
    }

    // -------------------------- fileName 的 get/set 方法 --------------------------
    std::string getFileName() const {
        return fileName;
    }

    void setFileName(const std::string& fileName) {
        this->fileName = fileName;
    }
    // -------------------------- fileSize 的 get/set 方法 --------------------------
    std::string getFileSize() const {
        return fileSize;
    }
    void setFileSize(const std::string& fileSize) {
        this->fileSize = fileSize;
    }
    // -------------------------- downloadResult 的 get/set 方法 --------------------------
    int getDownloadResult() const {
        return downloadResult;
    }
    void setDownloadResult(int downloadResult) {
        // 可选：增加参数合法性校验，确保值只能是 1/0/-1
        if (downloadResult == 1 || downloadResult == 0 || downloadResult == -1) {
            this->downloadResult = downloadResult;
        }
    }
    // -------------------------- localPath 的 get/set 方法 --------------------------
    std::string getLocalPath() const {
        return localPath;
    }
    void setLocalPath(const std::string& localPath) {
        this->localPath = localPath;
    }
    // -------------------------- processError 的 get/set 方法 --------------------------
    std::string getProcessError() const {
        return processError;
    }
    void setProcessError(const std::string& processError) {
        this->processError = processError;
    }
private:
    std::string cameraId;      // 摄像头ID
    std::string nvrId;         // NVR设备ID
    std::string fileName;      // 文件名
    std::string fileSize;      // 文件大小（字节）
    int downloadResult;        // 下载结果（1:完成，0:进行中 ,-1:失败）
    std::string localPath;     // 本地下载地址
    std::string processError;  // 下载过程中的错误信息
};


class VideoFrame {
public:
    // 提供构造函数方便创建帧对象
    VideoFrame(std::string cameraId_, std::string nvrId_, FrameData frame_, bool integrity_ = false)
        : cameraId(cameraId_)
        , nvrId(nvrId_)
        , frame(frame_)
        , integrity(integrity_) {}

    // 提供必要的访问器
    const std::string& getCameraId() const noexcept { return cameraId; }
    const std::string& getNvrId() const noexcept { return nvrId; }
    const FrameData& getFrameData() const noexcept { return frame; }
    bool isIntegrity() const noexcept { return integrity; }

private:
    std::string cameraId;
    std::string nvrId;
    FrameData frame;
    bool integrity = false;
};

class PreviewStream {
public:
    PreviewStream() = default;
    PreviewStream(std::string  cameraId, std::string  nvrId)
        : cameraId_(cameraId), nvrId_(nvrId) {}

    std::string  getCameraId() const noexcept { return cameraId_; }
    std::string  getNvrId() const noexcept { return nvrId_; }

    void setCameraId(std::string  id) noexcept { cameraId_ = id; }
    void setNvrId(std::string  id) noexcept { nvrId_ = id; }

private:
    std::string cameraId_;
    std::string nvrId_;
};
class PreviewFrame {
public:
    PreviewFrame() = default;

    std::string getCameraId() const noexcept { return cameraId_; }
    std::string getNvrId() const noexcept { return nvrId_; }
    const FrameData& getFrame() const noexcept { return frame_; }
    bool getIntegrity() const noexcept{return integrity_;}

    void setCameraId(std::string id) noexcept { cameraId_ = id; }
    void setNvrId(std::string id) noexcept { nvrId_ = id; }
    void setFrame(const FrameData& f) { frame_ = f; }
    void setIntegrity(bool integrity) noexcept {integrity_ = integrity;}

private:
    std::string cameraId_;
    std::string nvrId_;
    FrameData frame_;
    bool integrity_ = false;
};

class CameraStatusInfo {
public:
    CameraStatusInfo() = default;

    std::string getCameraId() const noexcept { return cameraId_; }
    std::string getNvrId() const noexcept { return nvrId_; }
    CameraStatus getStatus() const noexcept { return status_; }

    void setCameraId(std::string id) noexcept { cameraId_ = id; }
    void setNvrId(std::string id) noexcept { nvrId_ = id; }
    void setStatus(CameraStatus st) noexcept { status_ = st; }

private:
    std::string cameraId_;
    std::string nvrId_;
    CameraStatus status_;
};

class VideoFrames {
public:
    VideoFrames() = default;

    bool getSuccess() const noexcept { return success; }  // 修正拼写错误 sucess -> success
    const std::vector<VideoFrame>& getFrames() const noexcept { return frames_; }
    
    void clear() { frames_.clear(); }
    void setSuccess(bool success_) noexcept { success = success_; }
    // 新增添加帧的方法，避免直接操作私有容器
    void addFrame(VideoFrame frame) { frames_.emplace_back(frame); }

private:
    std::vector<VideoFrame> frames_;
    bool success = false; 
}; 

class VideoDerviceStatusInfo {
public:
    bool getNvrStatus() const  { return nvrStatus_; }
    const std::vector<CameraStatusInfo>& getCameraStatusList() const noexcept {
        return cameraStatusList_;
    }

    void setNvrStatus(bool st)  { nvrStatus_ = st; }
    void addCameraStatus(const CameraStatusInfo& info) {
        cameraStatusList_.push_back(info);
    }
    void clearCameraStatus() { cameraStatusList_.clear(); }

private:
    bool nvrStatus_;
    std::vector<CameraStatusInfo> cameraStatusList_;
};



class VideoFile {
public:
    // ===================== 核心常量与类型定义 =====================
    // 时间格式常量（统一管理，避免硬编码）
    static constexpr const char* TIME_FORMAT = "%Y-%m-%d %H:%M:%S";
    // 默认时间字符串
    static constexpr const char* DEFAULT_TIME_STR = "未设置";

    // ===================== 构造函数（重载，满足不同初始化场景） =====================
    // 默认构造：所有成员初始化为合理默认值
    VideoFile() : id(-1), fileSize(""), startTimeStamp(0), endTimeStamp(0) {}

    // 推荐构造：直接传入时间戳（高效，无转换开销）
    VideoFile(int id_, const std::string& fileName_, const std::string&  fileSize_, 
              time_t startTimeStamp_, time_t endTimeStamp_)
        : id(id_), fileName(fileName_), fileSize(fileSize_),
          startTimeStamp(startTimeStamp_), endTimeStamp(endTimeStamp_) {}

    // 便捷构造：传入时间字符串（自动转换为时间戳）
    VideoFile(int id_, const std::string& fileName_, const std::string& fileSize_, 
              const std::string& startTimeStr_, const std::string& endTimeStr_)
        : id(id_), fileName(fileName_), fileSize(fileSize_) {
        setStartTimeByStr(startTimeStr_);  // 自动转换字符串为时间戳
        setEndTimeByStr(endTimeStr_);
    }

    // ===================== 拷贝/移动语义（显式默认，清晰可控） =====================
    VideoFile(const VideoFile&) = default;
    VideoFile(VideoFile&&) = default;
    VideoFile& operator=(const VideoFile&) = default;
    VideoFile& operator=(VideoFile&&) = default;

    // ===================== 析构函数 =====================
    ~VideoFile() = default;

    // ===================== 访问器（Getter）- 分类清晰，语义明确 =====================
    // 基础属性
    int getId() const { return id; }
    const std::string& getFileName() const { return fileName; }
    const std::string getFileSize() const { return fileSize; }

    // 时间戳（原始值，高性能）
    time_t getStartTimeStamp() const { return startTimeStamp; }
    time_t getEndTimeStamp() const { return endTimeStamp; }

    // 时间字符串（格式化，易用性）
    std::string getStartTimeStr() const {
        return timeStampToStr(startTimeStamp);
    }
    std::string getEndTimeStr() const {
        return timeStampToStr(endTimeStamp);
    }

    // ===================== 修改器（Setter）- 支持两种时间设置方式 =====================
    // 基础属性
    void setId(int id_) { id = id_; }
    void setFileName(const std::string& fileName_) { fileName = fileName_; }
    void setFileSize(const std::string& fileSize_) { fileSize = fileSize_; }

    // 时间戳直接设置（高效）
    void setStartTimeStamp(time_t ts) { startTimeStamp = ts; }
    void setEndTimeStamp(time_t ts) { endTimeStamp = ts; }

    // 时间字符串设置（自动转换，易用）
    void setStartTimeByStr(const std::string& timeStr) {
        startTimeStamp = strToTimeStamp(timeStr);
    }
    void setEndTimeByStr(const std::string& timeStr) {
        endTimeStamp = strToTimeStamp(timeStr);
    }

    // ===================== 业务逻辑方法 =====================
    // 判断时间是否重叠（核心业务逻辑，保留）
    bool isTimeOverlap(time_t start, time_t end) const {
        // 边界处理：时间戳为0时视为无有效时间，不重叠
        if (startTimeStamp == 0 || endTimeStamp == 0) {
            return false;
        }
        return (startTimeStamp < end) && (endTimeStamp > start);
    }

private:
    // ===================== 私有成员（类型统一，语义清晰） =====================
    int id;                 // 序号（-1表示未初始化）
    std::string fileName;   // 文件名（含路径）
    std::string fileSize;      // 文件大小（字节）
    time_t startTimeStamp;  // 开始时间戳（原始数值，类型匹配）
    time_t endTimeStamp;    // 结束时间戳（原始数值，类型匹配）

    // ===================== 私有辅助方法（工具函数，封装细节） =====================
    // Linux专属：将time_t时间戳转换为指定格式的字符串（线程安全）
    std::string timeStampToStr(time_t ts) const {
        if (ts == 0) {
            return DEFAULT_TIME_STR;
        }

        std::tm tm_;
        // localtime_r是Linux线程安全版本，替代非线程安全的localtime
        if (localtime_r(&ts, &tm_) == nullptr) {
            return DEFAULT_TIME_STR;
        }

        std::ostringstream oss;
        oss << std::put_time(&tm_, TIME_FORMAT);
        return oss.str();
    }

    // Linux专属：将时间字符串转换为time_t时间戳（线程安全）
    time_t strToTimeStamp(const std::string& timeStr) const {
        if (timeStr.empty() || timeStr == DEFAULT_TIME_STR) {
            return 0;
        }

        std::tm tm_ = {0}; // 初始化所有字段为0，避免脏数据
        std::istringstream iss(timeStr);
        iss >> std::get_time(&tm_, TIME_FORMAT);

        // 解析失败返回0
        if (iss.fail()) {
            return 0;
        }

        // mktime转换tm为time_t，自动处理时区
        return mktime(&tm_);
    }
};

class VideoFiles {
public:
    // 构造/析构
    // 修正：初始化带前缀的成员变量 m_isSuccess
    VideoFiles() : m_isSuccess(false) {} 
    ~VideoFiles() = default;

    // 拷贝/移动
    VideoFiles(const VideoFiles&) = default;
    VideoFiles(VideoFiles&&) = default;
    VideoFiles& operator=(const VideoFiles&) = default;
    VideoFiles& operator=(VideoFiles&&) = default;

    // 容器操作：添加文件
    void addFile(const VideoFile& file) {
        m_videoFiles.push_back(file);
    }

    void addFile(VideoFile&& file) {
        m_videoFiles.emplace_back(std::move(file));
    }

    // 容器操作：清空列表
    void clear() {
        m_videoFiles.clear();
    }

    // 容器操作：获取文件数量
    size_t size() const {
        return m_videoFiles.size();
    }

    // 容器操作：判断是否为空
    bool empty() const {
        return m_videoFiles.empty();
    }

    // 访问文件列表（只读）
    const std::vector<VideoFile>& getFiles() const {
        return m_videoFiles;
    }

    // ===================== errorMsg 和 isSuccess 的 Getter/Setter =====================
    // 错误信息 - Setter
    void setErrorMsg(const std::string& msg) {
        m_errorMsg = msg;
    }

    // 错误信息 - Getter（只读，返回const引用避免拷贝）
    const std::string& getErrorMsg() const {
        return m_errorMsg;
    }

    // 查询状态 - Setter
    void setSuccess(bool success) {
        m_isSuccess = success; // 修正：赋值给带前缀的成员变量
    }

    // 查询状态 - Getter（函数名 isSuccess，变量名 m_isSuccess，避免重名）
    bool isSuccess() const {
        return m_isSuccess; // 修正：返回带前缀的成员变量
    }

    // 便捷方法：一键设置失败状态+错误信息（简化上层调用）
    void setFailed(const std::string& errMsg) {
        m_isSuccess = false; // 修正：赋值给带前缀的成员变量
        m_errorMsg = errMsg;
    }

    // 便捷方法：一键设置成功状态（清空错误信息）
    void setSucceeded() {
        m_isSuccess = true; // 修正：赋值给带前缀的成员变量
        m_errorMsg.clear();
    }

private:
    // 修正：所有私有成员加 m_ 前缀，避免和函数名冲突，符合C++命名规范
    std::vector<VideoFile> m_videoFiles;  // 视频文件列表
    std::string m_errorMsg;               // 错误信息（失败时填充）
    bool m_isSuccess;                     // 查询是否成功（加前缀区分函数名 isSuccess()）
};





//dao层的抽象的通道对象
class Camera {//通道 //解码相应的H264/H265 帧 ，保存最新的帧（RGB）,
public:
   Camera(const CameraInfo& info)
        : info_(info), status_(CameraStatus::OFFLINE), codecCtx_(nullptr)
          {
            initDecoder();
        frameYUV_ = av_frame_alloc();
    }

    ~Camera() {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if(frameYUV_) av_frame_free(&frameYUV_);
        if (codecCtx_) avcodec_free_context(&codecCtx_);
       
    }

    CameraStatus getStatus();
    // 你需要的业务
    bool getLastKeyFrame(FrameData& out);//得到最新帧

    void onEncodedFrame(uint8_t* Data, size_t len);// 海康SDK回调来的编码帧

    CameraInfo getCameraInfo(){return info_;}
private:
    void logFFmpegError(const char* func, int err);

    void updateKeyFrame(FrameData lastKeyFrame);

    void updateStatus(CameraStatus status);

    bool convertYUVToRGB(AVFrame* yuvFrame);

    bool initDecoder();
    
    AVCodecID detectCodec(uint8_t* data, size_t len);

    std::string cameraStatusToString(CameraStatus status);

private:
    CameraInfo info_;
    CameraStatus status_;
    std::mutex statusMutex_;

    // 最新帧
    FrameData lastKeyFrame_;//这个是保存帧的
    std::mutex frameMutex_;
    AVFrame* frameYUV_;  // YUV帧缓冲区（复用避免重复分配）
     // 解码器
    AVCodecContext* codecCtx_;
    std::mutex decoderMutex_; // 解码器操作锁
    bool decoderInitialized_ = false;
    friend class HKVDevice;  // 允许 NVR 层直接写入缓存（仅该类允许）
};
