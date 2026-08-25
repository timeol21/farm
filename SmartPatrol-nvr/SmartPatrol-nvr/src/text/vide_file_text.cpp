#include "config_parser.h"
#include "video_service.h"
#include <iostream>
#include <memory>
#include <iomanip>  // 用于格式化输出
#include <string>

// 辅助函数：格式化文件大小（字节转KB/MB/GB，提升可读性）
std::string formatFileSize(uint64_t bytes) {
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;

    if (bytes >= GB) {
        return std::to_string(bytes / GB).substr(0, 5) + " GB";
    } else if (bytes >= MB) {
        return std::to_string(bytes / MB).substr(0, 5) + " MB";
    } else if (bytes >= KB) {
        return std::to_string(bytes / KB).substr(0, 5) + " KB";
    } else {
        return std::to_string(bytes) + " B";
    }
}

int main() {
    try {
        // ========== 步骤1：加载配置文件 ==========
        std::cout << "【步骤1】加载配置文件..." << std::endl;
        bool configLoaded = ConfigParser::getInstance().loadFromFile(
            "/home/ztl/workspace/SmartPatrol-nvr/include/common/config/config.json"
        );
        if (!configLoaded) {
            std::cerr << "配置文件加载失败！请检查路径或文件格式。" << std::endl;
            return -1;
        }
        std::cout << "配置文件加载成功！" << std::endl;

        // ========== 步骤2：创建视频服务实例 ==========
        std::cout << "\n【步骤2】创建 VideoService 实例..." << std::endl;
        std::unique_ptr<IVideoService> videoService = std::make_unique<VideoService>();
        if (!videoService) {
            std::cerr << "VideoService 实例创建失败！" << std::endl;
            return -1;
        }
        std::cout << "VideoService 实例创建成功！" << std::endl;

        // ========== 步骤3：调用查询接口 ==========
        std::cout << "\n【步骤3】调用 queryRecordFiles 查询录像文件..." << std::endl;
        // 配置查询参数（摄像头ID、开始时间、结束时间，此处你传入的是空字符串，可根据需要修改）
        std::string cameraId = "2";
        std::string startTime = "2025-12-15 00:00:00";  // 空字符串表示不限制开始时间
        std::string endTime = "2025-12-15 23:59:59";    // 空字符串表示不限制结束时间
        VideoFiles videofiles;

        // 调用查询接口
        bool querySuccess = videoService->queryRecordFiles(cameraId, startTime, endTime, videofiles);

        // ========== 步骤4：输出查询结果 ==========
        std::cout << "\n【步骤4】输出录像文件查询结果 =========" << std::endl;
        if (!querySuccess || !videofiles.isSuccess()) {
            // 查询失败：输出错误信息
            std::cerr << "❌ 录像文件查询失败！" << std::endl;
            if (!videofiles.getErrorMsg().empty()) {
                std::cerr << "   错误信息：" << videofiles.getErrorMsg() << std::endl;
            }
            return -1;
        }

        

        // ========== 步骤5：退出测试 ==========
        std::cout << "\n【步骤5】测试完成，正常退出。" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ 测试过程中发生异常: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "\n❌ 测试过程中发生未知异常！" << std::endl;
        return -1;
    }
}