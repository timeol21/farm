#include <iostream>
#include "WaterLevelSensor.h"
#include "SmokeAlarm.h"
#include "InfraredSensor.h"
#include "HumitureSensor.h"
#include "DoorLock.h"

// 设备类型枚举
enum DeviceType {
    WATER_LEVEL,
    SMOKE_ALARM,
    INFRARED,
    HUMITURE,
    DOOR_LOCK
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "使用方法：" << std::endl;
        std::cout << "  " << argv[0] << " water_level   # 启动水位传感器监测" << std::endl;
        std::cout << "  " << argv[0] << " smoke         # 启动烟感报警器监测" << std::endl;
        std::cout << "  " << argv[0] << " infrared      # 启动红外传感器监测" << std::endl;
        std::cout << "  " << argv[0] << " humiture      # 启动温湿度传感器监测" << std::endl;
        std::cout << "  " << argv[0] << " door_lock     # 启动门锁控制" << std::endl;
        return 1;
    }

    std::string deviceType = argv[1];
    BaseDevice* device = nullptr;

    // 创建对应设备实例
    if (deviceType == "water_level") {
        device = new WaterLevelSensor();
    } else if (deviceType == "smoke") {
        device = new SmokeAlarm();
    } else if (deviceType == "infrared") {
        device = new InfraredSensor();
    } else if (deviceType == "humiture") {
        device = new HumitureSensor();
    } else if (deviceType == "door_lock") {
        device = new DoorLock();
    } else {
        std::cerr << "错误：无效的设备类型 " << deviceType << std::endl;
        return 1;
    }

    // 初始化设备
    if (!device->init()) {
        std::cerr << "设备初始化失败" << std::endl;
        delete device;
        return 1;
    }

    // 启动设备
    if (deviceType == "door_lock") {
        // 门锁进入交互模式
        dynamic_cast<DoorLock*>(device)->interactiveControl();
    } else {
        // 传感器启动轮询
        device->startPolling();
    }

    // 释放资源
    delete device;
    return 0;
}