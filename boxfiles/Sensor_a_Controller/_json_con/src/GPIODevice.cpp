#include "GPIODevice.h"
#include <iostream>
#include <limits>
#include <unistd.h>

GPIODevice::GPIODevice(const std::string& deviceId) 
    : BaseDevice(deviceId), initialValue(0) {}

bool GPIODevice::init() {
    // 1. 加载配置
    if (!configMgr.loadConfig()) {
        return false;
    }

    // 2. 查找GPIO配置
    if (!configMgr.findGPIOConfig(deviceId, deviceConfig)) {
        return false;
    }

    // 3. 初始化GPIO
    if (!gpio.initGPIO(deviceConfig)) {
        return false;
    }

    // 4. 设置初始电平
    initialValue = deviceConfig["initial_value"].get<int>();
    if (!gpio.setValue(initialValue)) {
        return false;
    }

    isInitialized = true;
    printDeviceInfo();
    return true;
}

bool GPIODevice::controlGPIO(int value) {
    if (!isInitialized) {
        std::cerr << "错误：GPIO设备未初始化" << std::endl;
        return false;
    }
    return gpio.setValue(value);
}

void GPIODevice::interactiveControl() {
    if (!isInitialized) {
        std::cerr << "错误：GPIO设备未初始化，无法进入交互模式" << std::endl;
        return;
    }

    std::cout << "\n=== 交互式控制模式 ===" << std::endl;
    std::cout << "操作说明：" << std::endl;
    std::cout << "  输入 1 → 高电平（3.3V）" << std::endl;
    std::cout << "  输入 0 → 低电平（0V）" << std::endl;
    std::cout << "  输入 q → 退出（恢复初始状态）" << std::endl;
    std::cout << "======================" << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n请输入控制指令（0/1/q）：";
        std::cin >> input;

        if (input == "1") {
            controlGPIO(1);
        } else if (input == "0") {
            controlGPIO(0);
        } else if (input == "q" || input == "Q") {
            std::cout << "退出交互模式，恢复初始电平..." << std::endl;
            controlGPIO(initialValue);
            break;
        } else {
            std::cout << "无效指令！请输入0、1或q" << std::endl;
        }

        // 清空输入缓冲区
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

void GPIODevice::printDeviceInfo() const {
    gpio.printGpioInfo();
}