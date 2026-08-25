#include <iostream>
#include <cstring>
#include <ctime>
#include <memory>
#include <iomanip>  // 用于格式化时间输出
#include "hikvision/hikDevice.h"
#include <thread>  // 必须加，否则sleep_for会编译报错
// 辅助函数：将时间戳转换为可读字符串（方便打印）
std::string timeStampToStr(time_t ts) {
    if (ts <= 0) return "无效时间";
    struct tm tm_buf;
    localtime_r(&ts, &tm_buf); // 线程安全的时间转换
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return std::string(buf);
}

int main() {
    // 1. 创建HKVDevice实例
    std::unique_ptr<INVR> hkNVR = std::make_unique<HKVDevice>();
    if (!hkNVR) {
        std::cerr << "[错误] 创建HKVDevice实例失败！" << std::endl;
        return -1;
    }

    // 2. 初始化SDK
    if (!hkNVR->initSDK()) {
        std::cerr << "[错误] SDK初始化失败！" << std::endl;
        return -1;
    }
    std::cout << "[信息] SDK初始化成功" << std::endl;

    // 3. 登录NVR
    bool loginOk = hkNVR->login("10.9.255.21", 8000, "admin", "Wlkjaqxy411");
    if (!loginOk) {
        std::cerr << "[错误] NVR登录失败（IP/端口/账号密码错误或网络不可达）！" << std::endl;
        hkNVR->deinitSDK();
        return -1;
    }
    std::cout << "[信息] NVR登录成功" << std::endl;

    // 4. 构造2025-12-15 00:00:00的时间戳（关键修复）
    struct tm targetTm = {0};
    targetTm.tm_year = 2025 - 1900; // 年份从1900开始计算
    targetTm.tm_mon = 12 - 1;       // 月份从0开始（12月=11）
    targetTm.tm_mday = 15;          // 日期
    targetTm.tm_hour = 0;
    targetTm.tm_min = 0;
    targetTm.tm_sec = 0;
    time_t date = mktime(&targetTm);
    if (date == -1) {
        std::cerr << "[错误] 时间戳转换失败！" << std::endl;
        hkNVR->logout();
        hkNVR->deinitSDK();
        return -1;
    }
    
    std::cout << "[信息] 目标查询日期：" << timeStampToStr(date) << std::endl;

    // // 5. 查询录像文件（适配VideoFiles类结构）
    VideoFileInfos result;
    bool queryResult  = hkNVR->queryRecordFiles(33, "2025-12-15 00:00:00","2025-12-15 23:59:59", result);
  
    std::cout << "===== 查询结果 =====" << std::endl;
    std::cout << "查询是否成功：" << (queryResult ? "是" : "否") << std::endl;
    std::cout << "结果状态：" << (result.isSuccess ? "成功" : "失败") << std::endl;
    std::cout << "错误信息：" << result.errorMsg << std::endl;
    std::cout << "找到文件数：" << result.fileList.size() << std::endl;

    // 打印每个文件的详细信息
    for (size_t i = 0; i < result.fileList.size(); ++i) {
        const auto& file = result.fileList[i];
        std::cout << "文件" << (i+1) << "：" << std::endl;
        std::cout << "  开始时间：" << file.starttime << std::endl;
        std::cout << "  结束时间：" << file.endtime << std::endl;
        std::cout << "  文件名：" << file.filename << std::endl;
        std::cout << "  文件大小：" << file.filesize << " 字节" << std::endl;
    };
    
    std::this_thread::sleep_for(std::chrono::seconds(1));

    DownloadVideoFile info("2", "nvr_001", result.fileList[0].filename,result.fileList[0].starttime,std::to_string(result.fileList[0].filesize));
    std::cout << "[信息] 准备启动下载..." << std::endl;

    // 启动下载
    if (!hkNVR->downloadRecordFile(info)) {
        std::cerr << "[错误] 下载任务已存在或启动失败" << std::endl;
    } else {
        std::cout << "[信息] 下载任务已启动" << std::endl;

        // 轮询下载进度
        while (!hkNVR->isDownloadFinished()) {
            int progress = hkNVR->returnDownloadprogress();
            std::cout << "[下载中] 进度: " << progress << "%" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // 下载完成，获取结果
        DownloadFile out = hkNVR->getDownloadResult();
        if (out.isSuccess()) {
            std::cout << "[成功] 下载完成！文件路径: " << out.localPath << std::endl;
        } else {
            std::cerr << "[失败] 下载失败，错误原因: " << out.processError << std::endl;
        }
    }

    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }

    // 6.资源清理
    hkNVR->logout();
    hkNVR->deinitSDK();
    std::cout << "[信息] NVR登出完成，SDK资源已释放" << std::endl;

    std::cout << "所有资源已释放，程序正常退出" << std::endl;
    return 0;
}