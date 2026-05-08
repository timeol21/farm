#include "DoorLock.h"
#include <iostream>

DoorLock::DoorLock(const std::string& deviceId) 
    : GPIODevice(deviceId) {}

void DoorLock::printDeviceInfo() const {
    std::cout << "---------------- 门锁设备信息 ----------------" << std::endl;
    std::cout << "设备ID：" << deviceId << std::endl;
    std::cout << "设备名称：" << deviceConfig["name"] << std::endl;
    std::cout << "GPIO组号：" << deviceConfig["gpio_group"] << std::endl;
    std::cout << "组内引脚：" << deviceConfig["gpio_pin_num"] << std::endl;
    std::cout << "计算物理编号：" << deviceConfig["gpio_group"].get<int>() * 32 + deviceConfig["gpio_pin_num"].get<int>() << std::endl;
    std::cout << "配置物理编号：" << deviceConfig["pin"] << std::endl;
    std::cout << "电压域：" << deviceConfig["voltage_domain"] << std::endl;
    std::cout << "有效电平：" << deviceConfig["active_logic"] << std::endl;
    std::cout << "初始电平：" << (initialValue == 1 ? "高电平（解锁）" : "低电平（上锁）") << std::endl;
    std::cout << "描述：" << deviceConfig["description"] << std::endl;
    std::cout << "----------------------------------------------" << std::endl;
}