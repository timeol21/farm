#include "hikvision/hikDevice.h"
#include "INVR.h"
// public

std::unique_ptr<INVR> NVRFactory::createNVR(const NVRConfig& nvrConfig) {
    if (nvrConfig.brand == "Hikvision") {
        // 创建海康NVR设备实例，可补充INFO日志记录创建成功（可选）
        LOG_INFO("创建海康NVR设备实例，品牌：" + nvrConfig.brand);
        return std::make_unique<HKVDevice>();
    } else {
        // 替换原生cout为LOG_ERROR，补充品牌信息（关键：明确是哪个品牌不支持）
        LOG_ERROR("不支持的NVR品牌，无法创建对应设备实例！请求的品牌：" + nvrConfig.brand);
        return nullptr;
    }
}


bool HKVDevice::initSDK() {
    // ========== 核心：配置项目内的OpenSSL库路径 ==========
    // 1. libcrypto（对应NET_SDK_INIT_CFG_LIBEAY_PATH）
    const char* libeay_path = "/home/ztl/workspace/SmartPatrol-nvr/lib/nvr/branks/hikvision/libcrypto.so.1.1";
    // 2. libssl（对应NET_SDK_INIT_CFG_SSLEAY_PATH，先确认同目录下是否有libssl.so.1.1）
    const char* ssleay_path = "/home/ztl/workspace/SmartPatrol-nvr/lib/nvr/branks/hikvision/libssl.so.1.1";
    
    // 检查库文件是否存在（可选，调试用）
    if (access(libeay_path, F_OK) == -1) {
        LOG_ERROR("libcrypto库不存在：" + std::string(libeay_path));
        return false;  // 原代码返回-1，此处修正为bool类型的false（函数返回值是bool）
    }
    if (access(ssleay_path, F_OK) == -1) {
        LOG_WARNING("libssl库不存在：" + std::string(ssleay_path) + "，尝试用系统库替代");
        ssleay_path = "/usr/lib/aarch64-linux-gnu/libssl.so.1.1"; // 系统库兜底
    }
    
    // ========== 设置SDK初始化参数 ==========
    // 配置OpenSSL加密库路径
    NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_LIBEAY_PATH, (void*)libeay_path);
    // 配置OpenSSL通信库路径
    NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_SSLEAY_PATH, (void*)ssleay_path);

    // 1. 初始化海康SDK
    if (!NET_DVR_Init()) {
        int err = NET_DVR_GetLastError();
        // 日志输出：错误码 + 描述（拼接字符串，保持信息完整）
        LOG_ERROR("[HKVDevice] SDK初始化失败，错误码：" + std::to_string(err));
        return false; 
    }

    // 2. 设置连接超时（2秒，1次重试）
    bool retConnect = NET_DVR_SetConnectTime(2000, 1);
    if (!retConnect) {
        int err = NET_DVR_GetLastError();
        LOG_WARNING("[HKVDevice] 设置连接超时失败，错误码：" + std::to_string(err));
    }

    // 3. 设置自动重连（10秒检测一次）
    bool retReconnect = NET_DVR_SetReconnect(10000, true);
    if (!retReconnect) {
        int err = NET_DVR_GetLastError();
        LOG_WARNING("[HKVDevice] 设置自动重连失败，错误码：" + std::to_string(err));
    }
    
    sdkInited_ = true;
    
    // 4. 初始化成功，记录日志 + 设置SDK原生日志
    LOG_INFO("[HKVDevice] SDK初始化成功，已配置OpenSSL库路径，启用SDK原生日志");
    NET_DVR_SetLogToFile(3, "./hik_sdk_log/", true);
    
    return true;
}

bool HKVDevice::deinitSDK() {
    // 1. 执行SDK清理
    if (!NET_DVR_Cleanup()) {
        int err = NET_DVR_GetLastError();
        // 替换std::cerr为LOG_ERROR，补充错误码
        LOG_ERROR("[HKVDevice] SDK反初始化失败，错误码：" + std::to_string(err));
        return false; // 清理失败，返回false
    }

    // 2. 清理成功，替换std::cout为LOG_INFO
    LOG_INFO("[HKVDevice] SDK反初始化成功");
    return true;
}


bool HKVDevice::login(const std::string& ip, short port, const std::string& user, const std::string& password) {
    // 1. 前置检查：SDK 未初始化则直接返回失败
    if (!sdkInited_) {
        LOG_ERROR("[HKVDevice::login] SDK 未初始化，无法执行登录操作");
        return false;
    }
    
    // 2. 参数合法性检查
    if (ip.empty() || port <= 0 || user.empty() || password.empty()) {
        LOG_ERROR("[HKVDevice::login] 登录参数无效（IP/端口/用户名/密码为空），IP：" + ip + "，端口：" + std::to_string(port));
        return false;
    }

    // 3. 初始化登录信息结构体
    NET_DVR_USER_LOGIN_INFO loginInfo = {0};
    // IP 地址（确保字符串终止符）
    strncpy(loginInfo.sDeviceAddress, ip.c_str(), sizeof(loginInfo.sDeviceAddress) - 1);
    loginInfo.sDeviceAddress[sizeof(loginInfo.sDeviceAddress) - 1] = '\0';

    loginInfo.wPort = port;

    // 用户名
    strncpy(loginInfo.sUserName, user.c_str(), sizeof(loginInfo.sUserName) - 1);
    loginInfo.sUserName[sizeof(loginInfo.sUserName) - 1] = '\0';

    // 密码
    strncpy(loginInfo.sPassword, password.c_str(), sizeof(loginInfo.sPassword) - 1);
    loginInfo.sPassword[sizeof(loginInfo.sPassword) - 1] = '\0';

    loginInfo.bUseAsynLogin = 0; // 同步登录

    // 4. 设备信息结构体（输出参数）
    NET_DVR_DEVICEINFO_V40 deviceInfo = {0};

    // 5. 执行登录
    userId_ = NET_DVR_Login_V40(&loginInfo, &deviceInfo);
    if (userId_ < 0) {
        int err = NET_DVR_GetLastError();
        // 替换std::cerr为LOG_ERROR，补充IP和错误码
        LOG_ERROR("[HKVDevice::login] 登录失败，IP：" + ip + "，端口：" + std::to_string(port) + "，错误码：" + std::to_string(err));
        nvrInited_ = false; // 登录失败，标记为未初始化
        return false;
    }

    // 6. 登录成功处理，替换std::cout为LOG_INFO
    LOG_INFO("[HKVDevice::login] 登录成功，用户ID：" + std::to_string(userId_) + "，设备IP：" + ip);
    nvrInited_ = true; // 仅登录成功时标记为已初始化
    return true;
}

bool HKVDevice::logout() {
    // 1. 检查是否已登录
    if (!nvrInited_ || userId_ < 0) {
        // 未登录属于正常场景，用LOG_WARNING（非错误，仅提醒）
        LOG_WARNING("[HKVDevice::logout] 设备未登录（用户ID：" + std::to_string(userId_) + "），无需执行注销操作");
        return true; // 无操作视为成功，避免误报失败
    }

    // 2. 执行注销操作
    BOOL ret = NET_DVR_Logout(userId_);
    if (ret == FALSE) { // 海康SDK：FALSE 表示失败，TRUE 表示成功
        int err = NET_DVR_GetLastError();
        // 替换std::cerr为LOG_ERROR，补充用户ID和错误码
        LOG_ERROR("[HKVDevice::logout] 注销失败，用户ID：" + std::to_string(userId_) + "，错误码：" + std::to_string(err));
        // 注销失败时不重置 userId_（可能需要重试），返回 false
        return false;
    }

    // 3. 注销成功，重置状态，替换std::cout为LOG_INFO
    LOG_INFO("[HKVDevice::logout] 注销成功，用户ID：" + std::to_string(userId_));
    userId_ = -1;
    nvrInited_ = false; // 重置 NVR 初始化状态

    return true;
}



bool HKVDevice::registerPassage(std::vector<CameraConfig> cameras){
    LOG_INFO("Start registerPassageS, camera count=" +
             std::to_string(cameras.size()));
    bool allSuccess = true;

    for (const auto& camera : cameras) {
        LOG_INFO("Register IPC start, ip=" + camera.IPCIP +
                 ", port=" + std::to_string(camera.port));

        if (!registerSingleChannel(
                camera.IPCIP,
                camera.port,
                camera.IPCUSER,
                camera.IPCPWD)) {

            LOG_ERROR("Register IPC failed, ip=" + camera.IPCIP);
            allSuccess = false;
        } else {
            LOG_INFO("Register IPC success, ip=" + camera.IPCIP);
        }
    }

    LOG_INFO("registerPassageS finished");
    return allSuccess;
}



bool HKVDevice::start(int channel, Camera* camera) {
    std::lock_guard<std::mutex> lock(ctxMutex_);
    
    // 补充INFO日志：记录启动操作的关键参数（通道号+摄像头信息）
    std::string cameraIp = camera ? camera->getCameraInfo().ip: "未知IP"; // 假设Camera类有getIp()方法，无则替换为标识
    LOG_INFO("[HKVDevice::start] 尝试启动NVR通道，通道号：" + std::to_string(channel) + "，摄像头IP：" + cameraIp);

    // 1. 检查通道是否已在运行
    auto it = channels_.find(channel);
    if (it != channels_.end() && it->second.realHandle > 0) {
        // 通道已运行，用WARNING级别提醒（非错误，仅告知重复操作）
        LOG_WARNING("[HKVDevice::start] 通道已在运行，无需重复启动，通道号：" + std::to_string(channel) + "，摄像头IP：" + cameraIp);
        return true;
    }

    // 2. 初始化通道上下文，启动拉流线程
    ChannelContext& ctx = channels_[channel];
    ctx.camera = camera;
    ctx.running = true;
    camera->updateStatus(CameraStatus::RUNNING);
    
    // 记录INFO日志：确认通道上下文初始化完成，即将启动拉流线程
    LOG_INFO("[HKVDevice::start] 通道上下文初始化完成，启动拉流线程，通道号：" + std::to_string(channel) + "，摄像头状态已设为RUNNING");
    
    ctx.pullThread = std::thread(&HKVDevice::pullRealPlayLoop, this, channel);
    return true;
}


bool HKVDevice::stop(int channel, Camera* camera) {
    std::lock_guard<std::mutex> lock(ctxMutex_);

    // 补充INFO日志：记录停止操作的关键参数
    std::string cameraIp = camera ? camera->getCameraInfo().ip :"未知IP"; // 无getIp()则替换为"通道"+std::to_string(channel)
    LOG_INFO("[HKVDevice::stop] 尝试停止NVR通道拉流，通道号：" + std::to_string(channel) + "，摄像头IP：" + cameraIp);

    // 前置检查：SDK/NVR未初始化
    if (!sdkInited_ || !nvrInited_) {
        LOG_ERROR("[HKVDevice::stop] SDK或NVR未初始化，无法停止通道拉流，通道号：" + std::to_string(channel));
        return false;
    }

    // 检查通道是否存在/有效
    auto it = channels_.find(channel);
    if (it == channels_.end() || it->second.realHandle < 0) {
        LOG_WARNING("[HKVDevice::stop] 通道无有效拉流句柄，无需停止，通道号：" + std::to_string(channel));
        return true;
    }

    // 停止拉流核心逻辑
    ChannelContext& ctx = channels_[channel];
    ctx.camera = camera;
    ctx.running = false;
    
    // 等待拉流线程退出
    if (ctx.pullThread.joinable()) {
        ctx.pullThread.join();
        LOG_INFO("[HKVDevice::stop] 通道拉流线程已正常退出，通道号：" + std::to_string(channel));
    }

    // 调用SDK停止实时播放
    int ret = NET_DVR_StopRealPlay(ctx.realHandle);
    if (ret <= 0) {
        int err = NET_DVR_GetLastError();
        // 替换std::cout为LOG_ERROR，补充通道号和错误码
        LOG_ERROR("[HKVDevice::stop] 停止实时播放失败，通道号：" + std::to_string(channel) + "，拉流句柄：" + std::to_string(ctx.realHandle) + "，错误码：" + std::to_string(err));
    } else {
        LOG_INFO("[HKVDevice::stop] 停止实时播放成功，通道号：" + std::to_string(channel) + "，拉流句柄：" + std::to_string(ctx.realHandle));
    }

    // 重置状态
    ctx.realHandle = -1;
    camera->updateStatus(CameraStatus::OFFLINE);
    LOG_INFO("[HKVDevice::stop] 通道停止完成，摄像头状态已设为OFFLINE，通道号：" + std::to_string(channel) + "，摄像头IP：" + cameraIp);

    return true;
}


bool HKVDevice::downloadRecordFile(DownloadVideoFile& info) {
    std::lock_guard<std::mutex> lock(downloadMutex_);

    // 补充INFO日志：记录下载请求的关键信息（如通道号、时间范围，需根据DownloadVideoFile结构体调整）
    std::string downloadInfo = "设备ID：" + info.cameraId() + "，开始时间：" + info.nvrId() ; // 适配你的info结构体字段
    LOG_INFO("[HKVDevice::downloadRecordFile] 接收到录像下载请求，" + downloadInfo);

    // 检查是否已有下载任务在运行
    if (runningDownload_.load()) {
        // 替换std::cerr为LOG_ERROR，补充当前下载任务信息
        LOG_ERROR("[HKVDevice::downloadRecordFile] 已有下载任务在运行，拒绝新的下载请求！" + downloadInfo);
        return false;
    }

    // 初始化下载状态
    runningDownload_.store(true);
    downloadProgress = 0;
    downloadId_ = -1;
    downloadResult_ = DownloadFile{};  // 清空上次结果

    LOG_INFO("[HKVDevice::downloadRecordFile] 初始化下载状态完成，启动下载线程，" + downloadInfo);

    // 启动下载线程（复制参数，避免外部生命周期问题）
    downloadThread_ = std::thread(&HKVDevice::downloadWorker, this, info);
    downloadThread_.detach(); // 按需detach/join

    LOG_INFO("[HKVDevice::downloadRecordFile] 下载线程已启动，任务状态：运行中，" + downloadInfo);
    return true;
}


int HKVDevice::returnDownloadprogress() {
    std::lock_guard<std::mutex> lock(downloadProgress_);
    // 补充INFO日志（可选，若需跟踪进度查询；频繁调用则建议改为DEBUG级别，或仅在调试时开启）
    // LOG_INFO("[HKVDevice::returnDownloadprogress] 查询下载进度，当前进度：" + std::to_string(downloadProgress) + "%");

    return downloadProgress;
}



bool HKVDevice::queryRecordFiles(int channel, std::string starttime, std::string endtime, VideoFileInfos& outFiles) {
    // 初始化输出参数
    outFiles.isSuccess = false;
    outFiles.errorMsg.clear();
    outFiles.fileList.clear();

    // 补充INFO日志：记录查询请求的核心参数
    LOG_INFO("[HKVDevice::queryRecordFiles] 接收到录像查询请求，通道号：" + std::to_string(channel) + 
             "，开始时间：" + starttime + "，结束时间：" + endtime);

    // 1. 基础校验
    if (userId_ < 0) {
        outFiles.errorMsg = "设备未登录，无法查询录像";
        LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
        return false;
    }
    if (channel <= 0) {
        outFiles.errorMsg = "通道号必须为正整数";
        LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，传入通道号：" + std::to_string(channel));
        return false;
    }

    try {
        // 2. 转换时间字符串为SDK时间结构体（复用Utils）
        LOG_INFO("[HKVDevice::queryRecordFiles] 开始转换查询时间格式，通道号：" + std::to_string(channel));
        NET_DVR_TIME lpStartTime = Utils::getNvrTime(starttime);
        NET_DVR_TIME lpStopTime = Utils::getNvrTime(endtime);
        LOG_INFO("[HKVDevice::queryRecordFiles] 时间格式转换完成，通道号：" + std::to_string(channel));

        // 3. 创建查找句柄（核心SDK调用）
        LONG lFindHandle = NET_DVR_FindFile(
            userId_,        // 登录句柄
            channel,        // 通道号
            0,              // 文件类型：0=所有类型
            &lpStartTime,   // 开始时间
            &lpStopTime     // 结束时间
        );

        // 句柄创建失败处理
        if (lFindHandle < 0) {
            int errorCode = NET_DVR_GetLastError();
            outFiles.errorMsg = "按时间查找录像文件失败，错误码：" + std::to_string(errorCode);
            LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
            
            // 释放无效句柄（防御性操作）
            if (lFindHandle >= 0) {
                NET_DVR_FindClose(lFindHandle);
            }
            return false;
        }
        LOG_INFO("[HKVDevice::queryRecordFiles] 查找句柄创建成功，句柄ID：" + std::to_string(lFindHandle) + 
                 "，通道号：" + std::to_string(channel));

        // 4. 循环遍历所有录像文件（核心查找逻辑）
        NET_DVR_FINDDATA_V40 lpFindData = {0};
        bool isFinding = true;

        while (isFinding) {
            LONG findResult = NET_DVR_FindNextFile_V40(lFindHandle, &lpFindData);

            switch (findResult) {
                case 1002: // 正在查找，请等待
                    LOG_WARNING("[HKVDevice::queryRecordFiles] 正在查找文件，请等待...，通道号：" + std::to_string(channel));
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    break;

                case 1000: { // 获取文件信息成功
                    VideoFileInfo fileInfo;
                    // 复用Utils转换时间格式
                    fileInfo.starttime = Utils::sdkTimeToStr(lpFindData.struStartTime);
                    fileInfo.endtime = Utils::sdkTimeToStr(lpFindData.struStopTime);
                    fileInfo.filename = std::string(lpFindData.sFileName);
                    fileInfo.filesize = lpFindData.dwFileSize;
                    // 添加到输出列表
                    outFiles.fileList.push_back(fileInfo);
                    LOG_INFO("[HKVDevice::queryRecordFiles] 找到1个录像文件，文件名：" + fileInfo.filename + 
                             "，文件大小：" + std::to_string(fileInfo.filesize) + "字节，通道号：" + std::to_string(channel));
                    break;
                }

                case 1003: // 没有更多文件，查找结束
                    LOG_INFO("[HKVDevice::queryRecordFiles] 没有更多的文件，查找结束，通道号：" + std::to_string(channel));
                    isFinding = false;
                    break;

                default: // 其他错误状态
                    outFiles.errorMsg = "查找文件异常，状态码：" + std::to_string(findResult);
                    LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
                    isFinding = false;
                    break;
            }
        }

        // 5. 释放查找句柄（必须执行，避免资源泄漏）
        if (lFindHandle >= 0) {
            NET_DVR_FindClose(lFindHandle);
            LOG_INFO("[HKVDevice::queryRecordFiles] 查找句柄已释放，句柄ID：" + std::to_string(lFindHandle) + 
                     "，通道号：" + std::to_string(channel));
        }

        // 6. 查询成功标记
        outFiles.isSuccess = true;
        outFiles.errorMsg = "查询成功，共找到" + std::to_string(outFiles.fileList.size()) + "个录像文件";
        LOG_INFO("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
        return true;

    } catch (const std::invalid_argument& e) { // 时间格式错误
        outFiles.errorMsg = "时间格式错误：" + std::string(e.what());
        LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
        return false;
    } catch (const std::exception& e) { // 其他异常
        outFiles.errorMsg = "查询失败：" + std::string(e.what());
        LOG_ERROR("[HKVDevice::queryRecordFiles] " + outFiles.errorMsg + "，通道号：" + std::to_string(channel));
        return false;
    }
}


bool HKVDevice::isDownloadFinished() {
    bool isFinished = !runningDownload_.load();
    // 补充INFO日志（若频繁调用可改为DEBUG级别，或仅在状态变化时记录）
    // LOG_INFO("[HKVDevice::isDownloadFinished] 查询下载任务状态，当前是否完成：" + std::string(isFinished ? "是" : "否"));
    return isFinished;
}




bool HKVDevice::ensureRecordPlan(int channel)
{
    LOG_INFO("[HKVDevice::ensureRecordPlan] start, channel=" +
             std::to_string(channel));

    NET_DVR_RECORD_V40 recordCfg;
    memset(&recordCfg, 0, sizeof(recordCfg));
    recordCfg.dwSize = sizeof(recordCfg);

    DWORD dwReturned = 0;

    /* ========== 1. 获取当前录像计划 ========== */
    if (!NET_DVR_GetDVRConfig(
            userId_,
            NET_DVR_GET_RECORDCFG_V40,
            channel,
            &recordCfg,
            sizeof(recordCfg),
            &dwReturned))
    {
        int err = NET_DVR_GetLastError();
        LOG_ERROR("[HKVDevice::ensureRecordPlan] Get config failed, channel=" +
                  std::to_string(channel) +
                  ", err=" + std::to_string(err));
        return false;
    }

    /* ========== 2. 幂等判断：已启用全天定时录像 ========== */
    if (recordCfg.dwRecord == 1)
    {
        bool allDayOK = true;
        for (int day = 0; day < MAX_DAYS; ++day)
        {
            if (recordCfg.struRecAllDay[day].byAllDayRecord != 1 ||
                recordCfg.struRecAllDay[day].byRecordType != 0)
            {
                allDayOK = false;
                break;
            }
        }

        if (allDayOK)
        {
            LOG_INFO("[HKVDevice::ensureRecordPlan] record plan already valid, channel=" +
                     std::to_string(channel));
            return true;
        }
    }

    LOG_INFO("[HKVDevice::ensureRecordPlan] configuring 7x24 record plan, channel=" +
             std::to_string(channel));

    /* ========== 3. 启用录像计划总开关 ========== */
    recordCfg.dwRecord = 1;

    /* ========== 4. 全局录像参数（推荐明确设置） ========== */
    recordCfg.byRecordManage = 0;   // 启用定时录像
    recordCfg.byStreamType   = 0;   // 主码流
    recordCfg.byAudioRec     = 1;   // 录音（如果 IPC 支持）
    recordCfg.dwPreRecordTime = 0;  // 不预录
    recordCfg.dwRecordTime   = 0;   // 不延时

    /* ========== 5. 配置 7 × 24 小时定时录像 ========== */
    for (int day = 0; day < MAX_DAYS; ++day)
    {
        /* 全天录像 */
        recordCfg.struRecAllDay[day].byAllDayRecord = 1;
        recordCfg.struRecAllDay[day].byRecordType  = 0; // 定时录像

        /* 清空时间段配置（全天录像不依赖它，但必须干净） */
        for (int seg = 0; seg < MAX_TIMESEGMENT_V30; ++seg)
        {
            NET_DVR_RECORDSCHED_V40& sched =
                recordCfg.struRecordSched[day][seg];
            memset(&sched, 0, sizeof(sched));
        }
    }

    /* ========== 6. 提交配置 ========== */
    if (!NET_DVR_SetDVRConfig(
            userId_,
            NET_DVR_SET_RECORDCFG_V40,
            channel,
            &recordCfg,
            sizeof(recordCfg)))
    {
        int err = NET_DVR_GetLastError();
        LOG_ERROR("[HKVDevice::ensureRecordPlan] Set config failed, channel=" +
                  std::to_string(channel) +
                  ", err=" + std::to_string(err));
        return false;
    }

    LOG_INFO("[HKVDevice::ensureRecordPlan] record plan configured OK, channel=" +
             std::to_string(channel));
    return true;
}



bool HKVDevice::ensureRecycleStorage()
{
    LOG_INFO("[HKVDevice::ensureRecycleStorage] 检查硬盘循环利用配置");

    NET_DVR_HDCFG hdCfg;
    memset(&hdCfg, 0, sizeof(hdCfg));
    hdCfg.dwSize = sizeof(hdCfg);

    DWORD retLen = 0;

    /* ========== 1. 获取当前硬盘配置 ========== */
    if (!NET_DVR_GetDVRConfig(
            userId_,
            NET_DVR_GET_HDCFG_V40,
            0,                          // 组号，从0开始
            &hdCfg,
            sizeof(hdCfg),
            &retLen))
    {
        int err = NET_DVR_GetLastError();
        LOG_ERROR("[HKVDevice::ensureRecycleStorage] 获取硬盘配置失败 err=" +
                  std::to_string(err));
        return false;
    }

    LOG_INFO("[HKVDevice::ensureRecycleStorage] 硬盘数量=" +
             std::to_string(hdCfg.dwHDCount));

    bool needSet = false;

    /* ========== 2. 遍历所有硬盘 ========== */
    for (DWORD i = 0; i < hdCfg.dwHDCount; ++i) {
        NET_DVR_SINGLE_HD& hd = hdCfg.struHDInfo[i];

        LOG_INFO("[HKVDevice::ensureRecycleStorage] 硬盘[" +
                 std::to_string(hd.dwHDNo) +
                 "] 状态=" + std::to_string(hd.dwHdStatus) +
                 " 循环=" + std::to_string(hd.byRecycling));

        /* 跳过异常/不可用硬盘 */
        if (hd.dwHdStatus != 0) {
            LOG_WARNING("[HKVDevice::ensureRecycleStorage] 硬盘[" +
                        std::to_string(hd.dwHDNo) +
                        "] 状态异常，跳过");
            continue;
        }

        if (hd.byRecycling == 0) {
            hd.byRecycling = 1;
            needSet = true;

            LOG_INFO("[HKVDevice::ensureRecycleStorage] 启用硬盘[" +
                     std::to_string(hd.dwHDNo) +
                     "] 循环利用");
        }
    }

    /* ========== 3. 如果无需修改，直接返回 ========== */
    if (!needSet) {
        LOG_INFO("[HKVDevice::ensureRecycleStorage] 所有硬盘已启用循环利用");
        return true;
    }

    /* ========== 4. 提交配置 ========== */
    if (!NET_DVR_SetDVRConfig(
            userId_,
            NET_DVR_SET_HDCFG,
            0xFFFFFFFF,                 // 无效通道
            &hdCfg,
            sizeof(hdCfg)))
    {
        int err = NET_DVR_GetLastError();
        LOG_ERROR("[HKVDevice::ensureRecycleStorage] 设置硬盘循环失败 err=" +
                  std::to_string(err));
        return false;
    }

    LOG_INFO("[HKVDevice::ensureRecycleStorage] 成功启用硬盘循环利用");
    return true;
}




// private

void HKVDevice::downloadWorker(DownloadVideoFile info) {
    // 补充INFO日志：记录下载任务启动，携带核心参数
    std::string cameraId = info.cameraId();
    std::string startTime = info.startTime();
    std::string fileName1 = info.fileName();
    LOG_INFO("[HKVDevice::downloadWorker] 下载线程启动，摄像头ID：" + cameraId + 
             "，开始时间：" + startTime + "，文件名：" + fileName1);

    char fileName[100] = {0};
    char savedFilePath[512] = {0};

    std::string realFileName = info.cameraId() + "_" + info.startTime();
    strncpy(fileName, info.fileName().c_str(), sizeof(fileName) - 1);

    // 拼接保存路径
    snprintf(
        savedFilePath,
        sizeof(savedFilePath),
        "/home/ztl/workspace/SmartPatrol-nvr/video_file/%s.mp4",
        realFileName.c_str()
    );
    LOG_INFO("[HKVDevice::downloadWorker] 拼接视频保存路径完成，路径：" + std::string(savedFilePath) + 
             "，摄像头ID：" + cameraId);

    // 检查文件是否已存在
    LOG_INFO("[HKVDevice::downloadWorker] 检查视频文件是否已存在，路径：" + std::string(savedFilePath));
    bool ok = FileUtils::fileExists(savedFilePath);
    if (ok) {
        LOG_WARNING("[HKVDevice::downloadWorker] 视频文件已存在，无需重复下载，路径：" + std::string(savedFilePath) + 
                    "，摄像头ID：" + cameraId);
        {
            std::lock_guard<std::mutex> lock(downloadMutex_);
            downloadId_ = -1;
            downloadResult_.downloadResult = 1;
            downloadResult_.localPath = savedFilePath;
            runningDownload_ = false;
        }
        LOG_INFO("[HKVDevice::downloadWorker] 已存在文件处理完成，下载任务终止，摄像头ID：" + cameraId);
        return;
    }

    // 1️⃣ 创建下载句柄
    LOG_INFO("[HKVDevice::downloadWorker] 开始创建下载句柄，文件名：" + info.fileName() + 
             "，保存路径：" + std::string(savedFilePath) + "，摄像头ID：" + cameraId);
    {
        std::lock_guard<std::mutex> lock(downloadMutex_);
        downloadId_ = NET_DVR_GetFileByName(
            userId_, fileName, savedFilePath
        );
    }

    if (downloadId_ < 0) {
        int errCode = NET_DVR_GetLastError(); // 补充错误码，便于定位
        LOG_ERROR("[HKVDevice::downloadWorker] 创建下载句柄失败！错误码：" + std::to_string(errCode) + 
                  "，摄像头ID：" + cameraId + "，保存路径：" + std::string(savedFilePath));
        {
            std::lock_guard<std::mutex> lock(downloadMutex_);
            downloadResult_.downloadResult = -1;
            downloadResult_.processError = "GetFileByName failed (错误码：" + std::to_string(errCode) + ")";
            runningDownload_ = false;
        }
        return;
    }
    LOG_INFO("[HKVDevice::downloadWorker] 下载句柄创建成功，句柄ID：" + std::to_string(downloadId_) + 
             "，摄像头ID：" + cameraId);

    // 2️⃣ 开始下载
    LOG_INFO("[HKVDevice::downloadWorker] 尝试启动下载，句柄ID：" + std::to_string(downloadId_) + 
             "，摄像头ID：" + cameraId);
    if (!NET_DVR_PlayBackControl_V40(
            downloadId_, NET_DVR_PLAYSTART,
            0, 0, nullptr, nullptr)) {

        int errCode = NET_DVR_GetLastError();
        LOG_ERROR("[HKVDevice::downloadWorker] 启动下载失败（PLAYSTART）！错误码：" + std::to_string(errCode) + 
                  "，句柄ID：" + std::to_string(downloadId_) + "，摄像头ID：" + cameraId);
        {
            std::lock_guard<std::mutex> lock(downloadMutex_);
            downloadResult_.downloadResult = -1;
            downloadResult_.processError = "PLAYSTART failed (错误码：" + std::to_string(errCode) + ")";
            NET_DVR_StopGetFile(downloadId_);
            runningDownload_ = false;
        }
        return;
    }
    LOG_INFO("[HKVDevice::downloadWorker] 下载已启动，开始轮询下载进度，句柄ID：" + std::to_string(downloadId_) + 
             "，摄像头ID：" + cameraId);

    // 3️⃣ 轮询进度
    while (runningDownload_) {
        int pos = NET_DVR_GetDownloadPos(downloadId_);
        if (pos >= 0) {
            downloadProgress = pos;
            // 进度每10%记录一次（避免日志刷屏），或改为DEBUG级别
            if (pos % 10 == 0) {
                LOG_INFO("[HKVDevice::downloadWorker] 下载进度更新，句柄ID：" + std::to_string(downloadId_) + 
                         "，摄像头ID：" + cameraId + "，当前进度：" + std::to_string(pos) + "%");
            }
        } else {
            int errCode = NET_DVR_GetLastError();
            LOG_WARNING("[HKVDevice::downloadWorker] 获取下载进度失败，句柄ID：" + std::to_string(downloadId_) + 
                        "，摄像头ID：" + cameraId + "，错误码：" + std::to_string(errCode));
        }
        if (pos >= 100) {
            LOG_INFO("[HKVDevice::downloadWorker] 下载进度已达100%，停止轮询，句柄ID：" + std::to_string(downloadId_) + 
                     "，摄像头ID：" + cameraId);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 4️⃣ 下载完成处理
    LOG_INFO("[HKVDevice::downloadWorker] 开始清理下载资源，句柄ID：" + std::to_string(downloadId_) + 
             "，摄像头ID：" + cameraId);
    {
        std::lock_guard<std::mutex> lock(downloadMutex_);
        NET_DVR_StopGetFile(downloadId_);
        downloadId_ = -1;
        downloadResult_.downloadResult = 1;
        downloadResult_.localPath = savedFilePath;
        runningDownload_ = false;
    }
    LOG_INFO("[HKVDevice::downloadWorker] 下载任务完成，视频保存路径：" + std::string(savedFilePath) + 
             "，摄像头ID：" + cameraId);
}

// ========== 2. 拉流循环线程函数 ==========
void HKVDevice::pullRealPlayLoop(int channel) {
    ChannelContext* ctx = nullptr;
    LOG_INFO("[HKVDevice::pullRealPlayLoop] 拉流线程启动，通道号：" + std::to_string(channel));

    // 前置检查：SDK/NVR初始化状态 + 通道上下文
    {
        std::lock_guard<std::mutex> lock(ctxMutex_);
        if (!sdkInited_ || !nvrInited_) {
            LOG_ERROR("[HKVDevice::pullRealPlayLoop] SDK或NVR未初始化，拉流线程退出，通道号：" + std::to_string(channel));
            return;
        }
        auto it = channels_.find(channel);
        if (it == channels_.end()) {
            LOG_ERROR("[HKVDevice::pullRealPlayLoop] 通道上下文不存在，拉流线程退出，通道号：" + std::to_string(channel));
            return;
        }
        ctx = &(it->second);
    }
    LOG_INFO("[HKVDevice::pullRealPlayLoop] 前置检查通过，开始初始化拉流参数，通道号：" + std::to_string(channel));

    // 初始化预览参数
    NET_DVR_PREVIEWINFO previewInfo = {0};
    previewInfo.lChannel = channel;          // 通道号（从33开始）
    previewInfo.dwStreamType = 0;            // 1-子码流（0-主码流）
    previewInfo.dwLinkMode = 0;              // 0-TCP方式
    previewInfo.hPlayWnd = 0;                // 不需要SDK解码显示，设为nullptr
    previewInfo.bBlocked = 0;                // 非阻塞模式
    previewInfo.byProtoType = 0;

    LOG_INFO("[HKVDevice::pullRealPlayLoop] 预览参数初始化完成，开始调用RealPlay_V40拉流，通道号：" + std::to_string(channel));
    // 调用SDK拉流
    int streamHandle = NET_DVR_RealPlay_V40(
        userId_,                // 登录句柄
        &previewInfo,           // 预览参数
        NULL,                   // 回调函数（无需解码显示）
        NULL                    // 实例指针
    );

    // 处理拉流结果
    {
        std::lock_guard<std::mutex> lock(ctxMutex_);
        LOG_INFO("[HKVDevice::pullRealPlayLoop] RealPlay_V40调用完成，流句柄：" + std::to_string(streamHandle) + 
                 "，通道号：" + std::to_string(channel));
        
        if (streamHandle < 0) {
            int err = NET_DVR_GetLastError();
            channels_[channel].realHandle = -1;
            channels_[channel].camera->updateStatus(CameraStatus::OFFLINE);
            LOG_ERROR("[HKVDevice::pullRealPlayLoop] 拉流失败！错误码：" + std::to_string(err) + 
                      "，通道号：" + std::to_string(channel) + "，流句柄：" + std::to_string(streamHandle));
            return;
        }
        channels_[channel].realHandle = streamHandle;
        LOG_INFO("[HKVDevice::pullRealPlayLoop] 拉流成功，流句柄已保存，通道号：" + std::to_string(channel) + 
                 "，流句柄：" + std::to_string(streamHandle));
    }

    // 设置帧回调函数
    int esCallbackRet = NET_DVR_SetESRealPlayCallBack(streamHandle, HKVDevice::onHKFrameCallback, ctx);
    if (esCallbackRet == 0) { // 假设0表示成功，需根据SDK文档调整
        int err = NET_DVR_GetLastError();
        LOG_WARNING("[HKVDevice::pullRealPlayLoop] 设置帧回调函数失败，返回值：" + std::to_string(err) + 
                    "，通道号：" + std::to_string(channel) + "，流句柄：" + std::to_string(streamHandle));
    } else {
        LOG_INFO("[HKVDevice::pullRealPlayLoop] 帧回调函数设置成功，通道号：" + std::to_string(channel) + 
                 "，流句柄：" + std::to_string(streamHandle));
    }

    // 拉流循环（持续运行直到停止信号）
    LOG_INFO("[HKVDevice::pullRealPlayLoop] 进入拉流循环，通道号：" + std::to_string(channel));
    while (true) {
        {
            std::lock_guard<std::mutex> lock(ctxMutex_);
            if (!channels_[channel].running) {
                LOG_INFO("[HKVDevice::pullRealPlayLoop] 收到停止信号，退出拉流循环，通道号：" + std::to_string(channel));
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    LOG_INFO("[HKVDevice::pullRealPlayLoop] 拉流线程正常退出，通道号：" + std::to_string(channel));
}


void CALLBACK HKVDevice::onHKFrameCallback(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX *pstruPackInfo, void *pUser)
{
    if (pUser == nullptr) {
        std::cerr << "onHKFrameCallback: dwUser is null!" << std::endl;
        return;
    }
    auto* ctx  = reinterpret_cast<HKVDevice::ChannelContext*>(pUser);
    
    if (ctx == nullptr || ctx->camera == nullptr) {
            return;
    }   
    
    if(pstruPackInfo->dwPacketType == 1){
        // std::cout << "ctx->streamHandle+" << ctx->realHandle << "ctx->camera->channel+" << ctx->camera->getCameraInfo().channel << std::endl;
        ctx->camera->onEncodedFrame(
            pstruPackInfo->pPacketBuffer,  // 完整NALU数
            pstruPackInfo->dwPacketSize   // 完整NALU长度
        );
        // dumpPacketBuffer(pstruPackInfo->pPacketBuffer,pstruPackInfo->dwPacketSize,ctx->camera->getCameraInfo().channel,pstruPackInfo->dwPacketType);
    }

}



DownloadFile HKVDevice::getDownloadResult(){
    std::lock_guard<std::mutex> lock(downloadMutex_);
    return downloadResult_;   // 返回副本，安全
}



bool Utils::parseToBytes(const std::string& input, size_t& outBytes)
{
    outBytes = 0;
    LOG_INFO("[Utils::parseToBytes] 开始解析字符串为字节数，输入字符串：" + (input.empty() ? "空字符串" : input));

    // 1. 空输入校验
    if (input.empty()) {
        LOG_ERROR("[Utils::parseToBytes] 解析失败：输入字符串为空");
        return false;
    }

    // 2. 去除空格 + 转大写
    std::string s = input;
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    LOG_INFO("[Utils::parseToBytes] 预处理后字符串（去空格+大写）：" + s);

    // 3. 提取数字部分
    size_t i = 0;
    bool hasDot = false;
    while (i < s.size()) {
        if (std::isdigit(s[i])) {
            i++;
        } else if (s[i] == '.' && !hasDot) {
            hasDot = true;
            i++;
        } else {
            break;
        }
    }

    // 无有效数字校验
    if (i == 0) {
        LOG_ERROR("[Utils::parseToBytes] 解析失败：输入字符串中无有效数字，预处理后字符串：" + s);
        return false; // 没有数字
    }

    // 4. 转换数字部分为浮点数
    std::string numStr = s.substr(0, i);
    double value = std::strtod(numStr.c_str(), nullptr);
    LOG_INFO("[Utils::parseToBytes] 提取数字部分：" + numStr + "，转换为浮点数：" + std::to_string(value));

    // 负数校验
    if (value < 0) {
        LOG_ERROR("[Utils::parseToBytes] 解析失败：提取的数字为负数，数字部分：" + numStr + "，值：" + std::to_string(value));
        return false;
    }

    // 5. 解析单位部分
    std::string unit = s.substr(i);
    LOG_INFO("[Utils::parseToBytes] 提取单位部分：" + (unit.empty() ? "空（默认B）" : unit));

    size_t multiplier = 1;
    if (unit.empty() || unit == "B") {
        multiplier = 1;
    } else if (unit == "KB") {
        multiplier = 1024ULL;
    } else if (unit == "MB") {
        multiplier = 1024ULL * 1024;
    } else if (unit == "GB") {
        multiplier = 1024ULL * 1024 * 1024;
    } else if (unit == "TB") {
        multiplier = 1024ULL * 1024 * 1024 * 1024;
    } else {
        LOG_ERROR("[Utils::parseToBytes] 解析失败：非法单位，单位部分：" + unit + "，支持的单位：B/KB/MB/GB/TB");
        return false; // 非法单位
    }
    LOG_INFO("[Utils::parseToBytes] 单位匹配成功，单位：" + (unit.empty() ? "B" : unit) + "，换算系数：" + std::to_string(multiplier));

    // 6. 计算总字节数
    double bytes = value * static_cast<double>(multiplier);
    if (bytes < 0) {
        LOG_ERROR("[Utils::parseToBytes] 解析失败：字节数计算结果为负数，数字值：" + std::to_string(value) + "，系数：" + std::to_string(multiplier) + "，计算结果：" + std::to_string(bytes));
        return false;
    }

    outBytes = static_cast<size_t>(bytes);
    LOG_INFO("[Utils::parseToBytes] 解析成功！输入字符串：" + input + "，预处理后：" + s + "，最终转换为字节数：" + std::to_string(outBytes));

    return true;
}



bool HKVDevice::registerSingleChannel(const std::string& ipcIp,int port,const std::string& user,const std::string& pwd){
    LOG_INFO("registerSingleChannel start, ip=" + ipcIp);

    NET_DVR_IPPARACFG_V40 ipCfg = {0};
    DWORD dwReturned = 0;

    if (!NET_DVR_GetDVRConfig(
            userId_,
            NET_DVR_GET_IPPARACFG_V40,
            0,
            &ipCfg,
            sizeof(ipCfg),
            &dwReturned)) {

        int err = NET_DVR_GetLastError();
        LOG_ERROR("Get IP config failed, ip=" + ipcIp +
                  ", err=" + std::to_string(err));
        return false;
    }

    for (DWORD i = 0; i < ipCfg.dwDChanNum; ++i) {
        if (ipCfg.struIPDevInfo[i].byEnable == 1) {
            std::string existIp =
                (char*)ipCfg.struIPDevInfo[i].struIP.sIpV4;
            if (existIp == ipcIp) {
                LOG_INFO("IPC already exists, ip=" + ipcIp +
                         ", channelIndex=" + std::to_string(i));
                return true;
            }
        }
    }

    int freeIndex = -1;
    for (DWORD i = 0; i < ipCfg.dwDChanNum; ++i) {
        if (ipCfg.struIPDevInfo[i].byEnable == 0) {
            freeIndex = i;
            break;
        }
    }

    if (freeIndex == -1) {
        LOG_ERROR("No free IP channel available, ip=" + ipcIp);
        return false;
    }

    NET_DVR_IPDEVINFO_V31& dev = ipCfg.struIPDevInfo[freeIndex];
    memset(&dev, 0, sizeof(dev));

    dev.byEnable = 1;
    dev.wDVRPort = port;
    strncpy((char*)dev.struIP.sIpV4, ipcIp.c_str(),
            sizeof(dev.struIP.sIpV4) - 1);
    strncpy((char*)dev.sUserName, user.c_str(),
            sizeof(dev.sUserName) - 1);
    strncpy((char*)dev.sPassword, pwd.c_str(),
            sizeof(dev.sPassword) - 1);

    if (!NET_DVR_SetDVRConfig(
            userId_,
            NET_DVR_SET_IPPARACFG_V40,
            0,
            &ipCfg,
            sizeof(ipCfg))) {

        int err = NET_DVR_GetLastError();
        LOG_ERROR("Set IP config failed, ip=" + ipcIp +
                  ", err=" + std::to_string(err));
        return false;
    }

    LOG_INFO("IPC added successfully, ip=" + ipcIp +
             ", channelIndex=" + std::to_string(freeIndex));
    return true;
}

