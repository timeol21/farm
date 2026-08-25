#include "video_service.h"
// private:

bool VideoService::addCamera(const CameraInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::addCamera] 尝试添加摄像头，摄像头ID：" + info.cameraId + 
             "，NVR ID：" + info.nvrId + "，通道号：" + std::to_string(info.channel));

    if (cameras_.count(info.cameraId)) {
        LOG_WARNING("[VideoService::addCamera] 摄像头已存在，无需重复添加，摄像头ID：" + info.cameraId);
        return false;
    }

    cameras_[info.cameraId] = std::make_unique<Camera>(info);
    LOG_INFO("[VideoService::addCamera] 摄像头添加成功，摄像头ID：" + info.cameraId + 
             "，当前摄像头总数：" + std::to_string(cameras_.size()));
    return true;
}

// ========== 2. 移除摄像头（私有） ==========
bool VideoService::removeCamera(const CameraInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::removeCamera] 尝试移除摄像头，摄像头ID：" + info.cameraId + 
             "，通道号：" + std::to_string(info.channel));

    auto it = cameras_.find(info.cameraId);
    if (it == cameras_.end()) {
        LOG_WARNING("[VideoService::removeCamera] 摄像头不存在，移除失败，摄像头ID：" + info.cameraId);
        return false;
    }

    if (!cameras_.count(info.cameraId)) {
        LOG_WARNING("[VideoService::removeCamera] 摄像头不存在，移除失败，摄像头ID：" + info.cameraId);
        return false;
    }

    // 停止该摄像头的拉流
    nvr_->stop(info.channel, it->second.get());
    LOG_INFO("[VideoService::removeCamera] 已停止摄像头拉流，摄像头ID：" + info.cameraId + 
             "，通道号：" + std::to_string(info.channel));

    cameras_.erase(info.cameraId);
    LOG_INFO("[VideoService::removeCamera] 摄像头移除成功，摄像头ID：" + info.cameraId + 
             "，当前摄像头总数：" + std::to_string(cameras_.size()));
    return true;
}

// ========== 3. 注册设备（私有核心函数） ==========
bool VideoService::registerDevices()
{
    LOG_INFO("[VideoService::registerDevices] ===== 开始注册NVR设备和摄像头 =====");

    /* ===================== 1. 读取配置 ===================== */
    LOG_INFO("[VideoService::registerDevices] 读取NVR/摄像头配置文件");
    auto& config = ConfigParser::getInstance().getConfig();

    if (config.cameras.empty()) {
        LOG_ERROR("[VideoService::registerDevices] 摄像头配置为空，无法继续");
        return false;
    }

    /* ===================== 2. 创建NVR对象 ===================== */
    LOG_INFO("[VideoService::registerDevices] 创建NVR设备，IP=" +
             config.nvr.ip + ", port=" + std::to_string(config.nvr.port));

    nvr_ = NVRFactory::createNVR(config.nvr);
    if (!nvr_) {
        LOG_ERROR("[VideoService::registerDevices] NVR设备创建失败");
        return false;
    }

    /* ===================== 3. 初始化 SDK ===================== */
    LOG_INFO("[VideoService::registerDevices] 初始化NVR SDK");
    if (!nvr_->initSDK()) {
        LOG_ERROR("[VideoService::registerDevices] NVR SDK 初始化失败");
        return false;
    }

    /* ===================== 4. 登录 NVR ===================== */
    LOG_INFO("[VideoService::registerDevices] 登录NVR，IP=" + config.nvr.ip +
             ", user=" + config.nvr.username);

    if (!nvr_->login(config.nvr.ip,
                     config.nvr.port,
                     config.nvr.username,
                     config.nvr.password))
    {
        LOG_ERROR("[VideoService::registerDevices] NVR 登录失败");
        return false;
    }

    LOG_INFO("[VideoService::registerDevices] NVR 登录成功");

    /* ===================== 5. 向 NVR 注册 IPC（幂等） ===================== */
    LOG_INFO("[VideoService::registerDevices] 开始向NVR注册摄像头（存在则跳过）");

    if (!nvr_->registerPassage(config.cameras)) {
        LOG_ERROR("[VideoService::registerDevices] 注册摄像头到NVR失败");
        return false;
    }

    LOG_INFO("[VideoService::registerDevices] 摄像头注册完成");

    /* ===================== 6. 确保录像计划（核心） ===================== */
    LOG_INFO("[VideoService::registerDevices] 配置录像计划（7x24，存在则跳过）");

    for (const auto& camera : config.cameras) {
        if (!nvr_->ensureRecordPlan(camera.channelNo)) {
            LOG_ERROR("[VideoService::registerDevices] 录像计划配置失败，channel=" +
                      std::to_string(camera.channelNo));
            // ⚠️ 不 return false：避免一个通道失败影响全部
        }
    }

    /* ===================== 7. 确保存储策略为循环覆盖 ===================== */
    LOG_INFO("[VideoService::registerDevices] 确保存储策略为循环覆盖");

    if (!nvr_->ensureRecycleStorage()) {
        LOG_ERROR("[VideoService::registerDevices] 设置存储覆盖策略失败");
        // 同样不致命
    }

    /* ===================== 8. 创建 Camera 对象并绑定 ===================== */
    LOG_INFO("[VideoService::registerDevices] 创建 Camera 对象，数量=" +
             std::to_string(config.cameras.size()));

    for (const auto& camCfg : config.cameras) {
        CameraInfo info;
        info.cameraId = camCfg.cameraId;
        info.nvrId    = camCfg.nvrId;
        info.channel  = camCfg.channelNo;
        info.name     = camCfg.name;

        if (!addCamera(info)) {
            LOG_ERROR("[VideoService::registerDevices] 添加Camera失败，cameraId=" +
                      camCfg.cameraId);
            continue;
        }
    }

    LOG_INFO("[VideoService::registerDevices] 摄像头创建完成，成功数量=" +
             std::to_string(cameras_.size()));

    LOG_INFO("[VideoService::registerDevices] ===== 注册流程完成 =====");
    return true;
}



// public:
VideoService::VideoService(){
    LOG_INFO("[VideoService::VideoService] 视频服务初始化，启动服务");
    start();
}

VideoService::~VideoService() {
    LOG_INFO("[VideoService::~VideoService] 视频服务析构，停止服务并释放NVR资源");
    stop();
    if (nvr_) {
        LOG_INFO("[VideoService::~VideoService] 登出NVR设备，释放SDK资源");
        nvr_->logout();
        nvr_->deinitSDK();
    }
}

void VideoService::start() {
    if (running_) {
        LOG_WARNING("[VideoService::start] 视频服务已在运行，无需重复启动");
        return;
    }
    running_ = true;
    LOG_INFO("[VideoService::start] 启动视频服务，开始注册设备");

    if (!registerDevices()) {
        LOG_ERROR("[VideoService::start] 设备注册失败，视频服务启动失败");
        running_ = false;
        return;
    }

    // 启动所有摄像头拉流
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::start] 开始启动所有摄像头拉流，摄像头总数：" + std::to_string(cameras_.size()));
    for (auto& camera : cameras_) {
        int channel = camera.second->getCameraInfo().channel;
        LOG_INFO("[VideoService::start] 启动摄像头拉流，摄像头ID：" + camera.first + 
                 "，通道号：" + std::to_string(channel));
        nvr_->start(channel, camera.second.get());
    }
    LOG_INFO("[VideoService::start] 视频服务启动完成，所有摄像头拉流已启动");
}

void VideoService::stop() {
    if (!running_) {
        LOG_WARNING("[VideoService::stop] 视频服务已停止，无需重复停止");
        return;
    }
    running_ = false;
    LOG_INFO("[VideoService::stop] 停止视频服务，停止所有摄像头拉流");

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& camera : cameras_) {
        int channel = camera.second->getCameraInfo().channel;
        LOG_INFO("[VideoService::stop] 停止摄像头拉流，摄像头ID：" + camera.first + 
                 "，通道号：" + std::to_string(channel));
        nvr_->stop(channel, camera.second.get());
    }
    LOG_INFO("[VideoService::stop] 视频服务停止完成，所有摄像头拉流已停止");
}

bool VideoService::getDeviceStatus(VideoDerviceStatusInfo& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::getDeviceStatus] 查询设备状态，当前摄像头总数：" + std::to_string(cameras_.size()));

    out.clearCameraStatus();
    if (!nvr_) {
        LOG_ERROR("[VideoService::getDeviceStatus] NVR实例为空，无法获取设备状态");
        return false;
    }

    // 设置NVR状态
    out.setNvrStatus(nvr_->getNVRStatus());
    LOG_INFO("[VideoService::getDeviceStatus] NVR状态已获取，状态：" + std::to_string(static_cast<int>(nvr_->getNVRStatus())));

    // 设置所有摄像头状态
    for (auto& kv : cameras_) {
        Camera& cam = *kv.second;
        CameraStatusInfo info;
        info.setCameraId(cam.getCameraInfo().cameraId);
        info.setNvrId(cam.getCameraInfo().nvrId);
        info.setStatus(cam.getStatus());
        out.addCameraStatus(info);

        LOG_INFO("[VideoService::getDeviceStatus] 摄像头状态已获取，摄像头ID：" + cam.getCameraInfo().cameraId + 
                 "，状态：" + std::to_string(static_cast<int>(cam.getStatus())));
    }
    LOG_INFO("[VideoService::getDeviceStatus] 设备状态查询完成，共返回" + std::to_string(out.getCameraStatusList().size()) + "个摄像头状态");
    return true;
}

bool VideoService::viewCameraPreviewStream(const PreviewStream& in, PreviewFrame& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::viewCameraPreviewStream] 查询单个摄像头预览帧，摄像头ID：" + in.getCameraId());

    if (!cameras_.count(in.getCameraId())) {
        LOG_ERROR("[VideoService::viewCameraPreviewStream] 摄像头不存在，查询失败，摄像头ID：" + in.getCameraId());
        return false;
    }

    Camera& camera = *cameras_[in.getCameraId()];
    FrameData frame;
    if (!camera.getLastKeyFrame(frame)) {
        LOG_WARNING("[VideoService::viewCameraPreviewStream] 摄像头无有效关键帧，摄像头ID：" + in.getCameraId() + 
                    "，通道号：" + std::to_string(camera.getCameraInfo().channel));
        return false;
    }

    // 封装输出帧
    out.setCameraId(in.getCameraId());
    out.setNvrId(in.getNvrId());
    out.setFrame(frame);
    out.setIntegrity(true);
    LOG_INFO("[VideoService::viewCameraPreviewStream] 摄像头预览帧获取成功，摄像头ID：" + in.getCameraId() + 
             "，帧宽：" + std::to_string(frame.width) + "，帧高：" + std::to_string(frame.height));
    return true;
}


bool VideoService::getAllLastKeyFrames(VideoFrames& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::getAllLastKeyFrames] 查询所有摄像头最新关键帧，摄像头总数：" + std::to_string(cameras_.size()));

    out.clear();
    out.setSuccess(true);
    bool allFramesOk = true;

    // 遍历所有摄像头
    for (auto& kv : cameras_) {
        Camera& cam = *kv.second;
        CameraInfo camInfo = cam.getCameraInfo();
        LOG_INFO("[VideoService::getAllLastKeyFrames] 处理摄像头，ID：" + camInfo.cameraId + 
                 "，通道号：" + std::to_string(camInfo.channel) + 
                 "，状态：" + std::to_string(static_cast<int>(cam.getStatus())));

        if (cam.getStatus() != CameraStatus::RUNNING) {
            LOG_WARNING("[VideoService::getAllLastKeyFrames] 摄像头未运行，跳过，摄像头ID：" + camInfo.cameraId + 
                        "，通道号：" + std::to_string(camInfo.channel) + 
                        "，状态：" + std::to_string(static_cast<int>(cam.getStatus())));
            allFramesOk = false;
            continue;
        }

        // 获取关键帧
        FrameData frameData;
        if (cam.getLastKeyFrame(frameData)) {
            VideoFrame videoFrame(
                camInfo.cameraId,
                camInfo.nvrId,
                frameData,
                true
            );
            out.addFrame(videoFrame);
            LOG_INFO("[VideoService::getAllLastKeyFrames] 摄像头关键帧获取成功，ID：" + camInfo.cameraId + 
                     "，帧宽：" + std::to_string(frameData.width) + "，帧高：" + std::to_string(frameData.height));
        } else {
            LOG_WARNING("[VideoService::getAllLastKeyFrames] 摄像头关键帧获取失败，ID：" + camInfo.cameraId);
            allFramesOk = false;
        }
    }

    out.setSuccess(allFramesOk);
    LOG_INFO("[VideoService::getAllLastKeyFrames] 所有摄像头关键帧查询完成，成功获取：" + std::to_string(out.getFrames().size()) + 
             "个，全部成功：" + std::string(allFramesOk ? "是" : "否"));
    return allFramesOk;
}

bool VideoService::queryRecordFiles(std::string cameraId, std::string startTime, std::string endTime, VideoFiles& outFiles) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::queryRecordFiles] 查询录像文件，摄像头ID：" + cameraId + 
             "，开始时间：" + startTime + "，结束时间：" + endTime);

    // 1. 检查NVR实例
    if (!nvr_) {
        std::string errMsg = "NVR instance is null";
        outFiles.setErrorMsg(errMsg);
        outFiles.setSuccess(false);
        LOG_ERROR("[VideoService::queryRecordFiles] " + errMsg);
        return false;
    }

    // 2. 检查摄像头ID
    auto cameraIt = cameras_.find(cameraId);
    if (cameraIt == cameras_.end()) {
        std::string errMsg = "Camera ID " + cameraId + " not found";
        outFiles.setErrorMsg(errMsg);
        outFiles.setSuccess(false);
        LOG_ERROR("[VideoService::queryRecordFiles] " + errMsg);
        return false;
    }

    // 3. 清空输出参数
    outFiles.clear();
    outFiles.setSuccess(false);
    outFiles.setErrorMsg("");

    // 4. 调用NVR查询接口
    VideoFileInfos videofiles;
    videofiles.fileList.clear();
    int channel = cameraIt->second->getCameraInfo().channel;
    LOG_INFO("[VideoService::queryRecordFiles] 调用NVR查询接口，通道号：" + std::to_string(channel));
    
    int ret = nvr_->queryRecordFiles(channel, startTime, endTime, videofiles);

    // 5. 检查查询结果
    if (ret <= 0 || !videofiles.isSuccess) {
        std::string errMsg = videofiles.errorMsg.empty() ? "NVR query failed, ret=" + std::to_string(ret) : videofiles.errorMsg;
        outFiles.setErrorMsg(errMsg);
        outFiles.setSuccess(false);
        LOG_ERROR("[VideoService::queryRecordFiles] 录像查询失败，" + errMsg + 
                  "，摄像头ID：" + cameraId + "，通道号：" + std::to_string(channel));
        return false;
    }

    // 6. 转换查询结果
    int fileId = 1;
    LOG_INFO("[VideoService::queryRecordFiles] NVR查询成功，找到" + std::to_string(videofiles.fileList.size()) + "个录像文件");
    for (const auto& srcFile : videofiles.fileList) {
        VideoFile destFile(
            fileId++,
            srcFile.filename,
            formatFileSize(srcFile.filesize),
            srcFile.starttime,
            srcFile.endtime
        );
        outFiles.addFile(std::move(destFile));
        LOG_INFO("[VideoService::queryRecordFiles] 转换录像文件信息，文件名：" + srcFile.filename + 
                 "，文件大小：" + formatFileSize(srcFile.filesize) + "，时间段：" + srcFile.starttime + " - " + srcFile.endtime);
    }

    // 7. 设置成功状态
    outFiles.setSuccess(true);
    outFiles.setErrorMsg("");
    LOG_INFO("[VideoService::queryRecordFiles] 录像查询完成，共返回" + std::to_string(outFiles.size()) + "个文件，摄像头ID：" + cameraId);
    return true;
}

bool VideoService::downloadRecordFile(DownloadVideoFile& in, DownloadReadyFile& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("[VideoService::downloadRecordFile] 开始下载录像文件，摄像头ID：" + in.cameraId() + 
             "，文件名：" + in.fileName() + "，文件大小：" + in.fileSize());

    // 1. 检查NVR实例
    if (!nvr_) {
        LOG_ERROR("[VideoService::downloadRecordFile] NVR实例为空，下载失败");
        return false;
    }

    // 启动下载任务
    bool ok = nvr_->downloadRecordFile(in);
    if (!ok) {
        LOG_ERROR("[VideoService::downloadRecordFile] 启动下载失败，摄像头ID：" + in.cameraId() + 
                  "，文件名：" + in.fileName());
        return false;
    } else {
        LOG_INFO("[VideoService::downloadRecordFile] 下载任务已启动，摄像头ID：" + in.cameraId() + 
                 "，文件名：" + in.fileName());
    }

    // 轮询下载进度
    LOG_INFO("[VideoService::downloadRecordFile] 开始轮询下载进度，摄像头ID：" + in.cameraId());
    while (!nvr_->isDownloadFinished()) {
        int progress = nvr_->returnDownloadprogress();
        LOG_INFO("[VideoService::downloadRecordFile] 下载进度更新，摄像头ID：" + in.cameraId() + 
                 "，进度：" + std::to_string(progress) + "%");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 获取下载结果
    LOG_INFO("[VideoService::downloadRecordFile] 下载完成，获取下载结果，摄像头ID：" + in.cameraId());
    DownloadFile downLoad = nvr_->getDownloadResult();

    // 封装输出结果
    out.setCameraId(in.cameraId());
    out.setNvrId(in.nvrId());
    out.setFileName(in.fileName());
    out.setFileSize(in.fileSize());
    out.setDownloadResult(downLoad.downloadResult);
    out.setLocalPath(downLoad.localPath);
    out.setProcessError(downLoad.processError);

    LOG_INFO("[VideoService::downloadRecordFile] 下载结果封装完成，摄像头ID：" + in.cameraId() + 
             "，本地路径：" + downLoad.localPath + 
             "，下载结果：" + std::to_string(downLoad.downloadResult) + 
             (downLoad.processError.empty() ? "" : "，错误信息：" + downLoad.processError));
    return true;
}


std::string VideoService::formatFileSize(uint64_t bytes) {
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    std::string sizeStr;
    if (bytes >= GB) {
        sizeStr = std::to_string(bytes / GB).substr(0, 5) + " GB";
    } else if (bytes >= MB) {
        sizeStr = std::to_string(bytes / MB).substr(0, 5) + " MB";
    } else if (bytes >= KB) {
        sizeStr = std::to_string(bytes / KB).substr(0, 5) + " KB";
    } else {
        sizeStr = std::to_string(bytes) + " B";
    }
    // LOG_DEBUG("[VideoService::formatFileSize] 文件大小格式化，原始字节数：%lu，格式化后：%s", 
    //       bytes, sizeStr.c_str()); 
    return sizeStr;
}


// ========== 14. 获取下载进度 ==========
int VideoService::getDownloadProgress() {
    if (!nvr_) {
        // LOG_WARNING("[VideoService::getDownloadProgress] NVR实例为空，返回进度：-1");
        return -1;
    }
    int progress = nvr_->returnDownloadprogress();
    // LOG_INFO("[VideoService::getDownloadProgress] 获取下载进度，当前进度：" + std::to_string(progress) + "%");
    return progress;
}