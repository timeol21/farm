#include "BaseDevice.h"
#include <iostream>
#include <chrono>
#include <thread>

BaseDevice::BaseDevice(const std::string& deviceId) 
    : configMgr(ConfigManager::getInstance()), 
      deviceId(deviceId), 
      isInitialized(false) {
    // 初始化编码（全局只执行一次）
    static bool encodingInited = false;
    if (!encodingInited) {
        ConfigManager::initEncoding();
        encodingInited = true;
    }
}

void BaseDevice::startPolling() {
    if (!isInitialized) {
        std::cerr << "错误：设备未初始化，无法启动轮询" << std::endl;
        return;
    }

    int interval = configMgr.getPollingInterval();
    std::cout << "=== 设备 " << deviceId << " 轮询启动 ===" << std::endl;
    std::cout << "轮询间隔：" << interval << "ms | 按Ctrl+C退出" << std::endl;
    std::cout << "============================================" << std::endl;

    try {
        while (true) {
            std::cout << "\n--- 轮询周期开始 ---" << std::endl;
            readData();
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            std::cout << "---------------------" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "\n轮询停止：" << e.what() << std::endl;
    } catch (...) {
        std::cout << "\n轮询停止：用户中断（Ctrl+C）" << std::endl;
    }
}

bool BaseDevice::isInitSuccess() const {
    return isInitialized;
}