#include "config_parser.h"
#include "video_service.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>

// 测试工具函数：转换 CameraStatus 为字符串
std::string cameraStatusToString(CameraStatus status) {
    switch (status) {
        case CameraStatus::ONLINE:  return "在线";
        case CameraStatus::RUNNING: return "运行";
        case CameraStatus::OFFLINE: return "离线";
        default:                    return "未知状态";
    }
}

// 测试工具函数：打印设备状态
void printDeviceStatus(const VideoDerviceStatusInfo& status) {
    std::cout << "\n=== 设备状态信息 ===" << std::endl;
    std::cout << "NVR 状态: " << (status.getNvrStatus() ? "在线" : "离线") << std::endl;
    
    const auto& cameraList = status.getCameraStatusList();
    std::cout << "摄像头总数: " << cameraList.size() << std::endl;
    for (const auto& cam : cameraList) {
        std::cout << "摄像头 [NVR:" << cam.getNvrId() << ", ID:" << cam.getCameraId() 
                  << "] 状态: " << cameraStatusToString(cam.getStatus()) << std::endl;
    }
    std::cout << "====================" << std::endl;
}

// 测试工具函数：打印帧信息
void printFrameInfo(const PreviewFrame& frame, const std::string& frameType) {
    std::cout << "\n=== " << frameType << " 帧信息 ===" << std::endl;
    std::cout << "所属 NVR: " << frame.getNvrId() << std::endl;
    std::cout << "摄像头 ID: " << frame.getCameraId() << std::endl;
    
    const auto& frameData = frame.getFrame();
    std::cout << "帧分辨率: " << frameData.width << "x" << frameData.height << std::endl;
    std::cout << "帧时间戳: " << frameData.lastKeyFrameTime << " ms" << std::endl;
    std::cout << "AVFrame 指针: " << frameData.frame.get() << std::endl;
    std::cout << "====================" << std::endl;
}

// 测试工具函数：打印所有关键帧信息
void printAllKeyFrames(const VideoFrames& frames) {
    std::cout << "\n=== 所有摄像头关键帧信息 ===" << std::endl;
    const auto& frameVec = frames.getFrames();  // 改为vector
    std::cout << "关键帧总数: " << frameVec.size() << std::endl;
    
    // 遍历vector（替代原Map遍历）
    for (size_t i = 0; i < frameVec.size(); ++i) {
        const VideoFrame& videoFrame = frameVec[i];
        const FrameData& frameData = videoFrame.getFrameData();
        
        std::cout << "[" << i+1 << "] 摄像头 ID: " << videoFrame.getCameraId() << std::endl;
        std::cout << "      分辨率: " << frameData.width << "x" << frameData.height << std::endl;
        std::cout << "      时间戳: " << frameData.lastKeyFrameTime << " ms" << std::endl;
        std::cout << "      AVFrame 指针: " << frameData.frame.get() << std::endl;
    }
    std::cout << "============================" << std::endl;
}

/**
 * @brief 带重试机制的帧获取函数
 * @tparam Func 获取帧的函数类型
 * @tparam Req 请求参数类型
 * @tparam Resp 响应数据类型
 * @param desc 操作描述（用于日志）
 * @param req 请求参数
 * @param resp 输出的响应数据
 * @param func 实际执行获取操作的函数
 * @param max_retry 最大重试次数
 * @param interval_ms 重试间隔（毫秒）
 * @return 是否获取成功
 */
template <typename Func, typename Req, typename Resp>
bool getFrameWithRetry(const std::string& desc, const Req& req, Resp& resp, Func func, 
                       int max_retry = 10, int interval_ms = 500) {
    for (int i = 0; i < max_retry; ++i) {
        std::cout << "\n[" << desc << "] 尝试获取 (第 " << i + 1 << "/" << max_retry << " 次)..." << std::endl;
        if (func(req, resp)) {
            std::cout << "[" << desc << "] 获取成功！" << std::endl;
            return true;
        }
        std::cout << "[" << desc << "] 获取失败，" << interval_ms << "ms 后重试..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    std::cerr << "[" << desc << "] 达到最大重试次数 (" << max_retry << " 次)，获取失败！" << std::endl;
    return false;
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

        // // ========== 步骤3：启动视频服务 ==========
        // std::cout << "\n【步骤3】启动视频服务..." << std::endl;
        // videoService->start();
        // std::cout << "视频服务启动完成！" << std::endl;

        // ========== 步骤4：获取设备状态（并等待设备就绪） ==========
        std::cout << "\n【步骤4】获取设备状态并等待设备就绪..." << std::endl;
        VideoDerviceStatusInfo deviceStatus;
        const int DEVICE_READY_RETRY = 5;  // 最大重试20次
        const int DEVICE_READY_INTERVAL = 7*1000;  // 每次间隔1秒
        bool deviceReady = false;
        
        for (int i = 0; i < DEVICE_READY_RETRY; ++i) {
            if (videoService->getDeviceStatus(deviceStatus)) {
                printDeviceStatus(deviceStatus);
                
                // 检查是否有在线且运行的摄像头
                bool hasRunningCamera = false;
                const auto& cameraList = deviceStatus.getCameraStatusList();
                for (const auto& cam : cameraList) {
                    if (cam.getStatus() == CameraStatus::RUNNING) {
                        hasRunningCamera = true;
                        break;
                    }
                }
                
                if (hasRunningCamera) {
                    deviceReady = true;
                    std::cout << "\n✅ 设备已就绪，存在运行中的摄像头！" << std::endl;
                    break;
                } else {
                    std::cout << "\n⚠️  设备状态已获取，但暂无运行中的摄像头，" << DEVICE_READY_INTERVAL << "ms 后重试..." << std::endl;
                }
            } else {
                std::cout << "\n⚠️  获取设备状态失败，" << DEVICE_READY_INTERVAL << "ms 后重试..." << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(DEVICE_READY_INTERVAL));
        }
        
        if (!deviceReady) {
            std::cerr << "\n❌ 等待设备就绪超时，退出测试！" << std::endl;
            videoService->stop();
            return -1;
        }
        std::cout << "\n⚠️  等待拉流预热，生成有效帧数据..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3)); // 3秒预热

        // ========== 步骤5：获取单个摄像头预览帧（带重试） ==========
        std::cout << "\n【步骤5】获取摄像头预览帧（NVR:5, Camera:2）..." << std::endl;
        PreviewStream previewReq;
        previewReq.setNvrId("nvr_001");     
        previewReq.setCameraId("2");  
        std::cout<<"CameraId+ "<<previewReq.getCameraId()<<std::endl;
        PreviewFrame previewFrame;
        // 绑定成员函数：std::bind 适配 getFrameWithRetry 的函数参数要求
        auto previewFunc = std::bind(&IVideoService::viewCameraPreviewStream, videoService.get(), 
                                     std::placeholders::_1, std::placeholders::_2);
        bool previewGot = getFrameWithRetry("预览帧", previewReq, previewFrame, previewFunc, DEVICE_READY_RETRY, DEVICE_READY_INTERVAL);
        if (previewGot) {
            printFrameInfo(previewFrame, "预览");
        }

        // ========== 步骤6：获取所有摄像头最新关键帧（带重试） ==========
        std::cout << "\n【步骤6】获取所有摄像头最新关键帧..." << std::endl;
        VideoFrames allKeyFrames;
        auto keyFrameFunc = std::bind(&IVideoService::getAllLastKeyFrames, videoService.get(), 
                                      std::placeholders::_1);
        // 注意：这里需要适配 getFrameWithRetry 的参数（req 可以传空，实际函数可能不需要req，需根据实际情况调整）
        // 临时封装：如果 getAllLastKeyFrames 只有一个输出参数，构造一个空的req
        struct EmptyReq {};
        EmptyReq emptyReq;
        auto wrappedKeyFrameFunc = [&](const EmptyReq&, VideoFrames& frames) {
            return videoService->getAllLastKeyFrames(frames);
        };
        
        auto start = std::chrono::high_resolution_clock::now();
        bool keyFramesGot = getFrameWithRetry("所有关键帧", emptyReq, allKeyFrames, wrappedKeyFrameFunc, 10, 500);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (keyFramesGot) {
            printAllKeyFrames(allKeyFrames);
            std::cout << "获取所有关键帧总耗时: " << duration.count() << " ms" << std::endl;
        }

        // ========== 步骤7：停止视频服务 ==========
        std::cout << "\n【步骤7】停止视频服务..." << std::endl;
        // videoService->stop();
        std::cout << "视频服务停止完成！" << std::endl;

        std::cout << "\n✅ 所有测试步骤执行完毕！" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ 测试过程中发生异常: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "\n❌ 测试过程中发生未知异常！" << std::endl;
        return -1;
    }
}