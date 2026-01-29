#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include "DeviceFactory.h"

int main() {
    std::cout << "=== 系统启动：开始初始化设备队列 ===" << std::endl;

    // 存储所有设备的容器（父类指针指向子类对象）
    std::vector<Device*> devices;

    // 1. 通过工厂创建设备并加入队列
    // PLC 上的传感器 (ttyS4)
    devices.push_back(DeviceFactory::createSensor("Smoke", "烟雾报警器", "/dev/ttyS4"));
    devices.push_back(DeviceFactory::createSensor("Water", "水位报警器", "/dev/ttyS4"));

    // USB 上的传感器 (ttyUSB10)
    devices.push_back(DeviceFactory::createSensor("Humiture", "环境温湿度", "/dev/ttyUSB10"));

    // PLC 上的控制器 (ttyS4)
    devices.push_back(DeviceFactory::createController("电磁阀1", "/dev/ttyS4", 0x0500));
    devices.push_back(DeviceFactory::createController("电磁阀2", "/dev/ttyS4", 0x0501));

    std::cout << "成功加载 " << devices.size() << " 个设备。" << std::endl;
    std::cout << "进入主循环轮询状态..." << std::endl;

    // 2. 无限循环轮询
    while (true) {
        std::cout << "\n--- [新一轮检测] ---" << std::endl;
        
        for (Device* dev : devices) {
            if (dev != nullptr) {
                dev->execute(); // 自动调用对应设备的业务逻辑
            }
            // 每个设备执行完稍微歇一会儿，防止串口压力太大
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // 每轮大循环间隔1秒
    }

    // 释放内存（虽然在无限循环中跑不到这里）
    for (Device* dev : devices) {
        delete dev;
    }

    return 0;
}

/*g++ main.cpp -o box_system -std=c++11*/