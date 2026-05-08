#include "SerialDirectDevice.h"
#include <iostream>

SerialDirectDevice::SerialDirectDevice(const std::string& deviceId) 
    : BaseDevice(deviceId), serialPort("") {}

bool SerialDirectDevice::init() {
    // 1. 加载配置
    if (!configMgr.loadConfig()) {
        return false;
    }

    // 2. 查找串口直连配置
    if (!configMgr.findSerialDirectConfig(deviceId, deviceConfig)) {
        return false;
    }

    // 3. 获取串口配置
    serialPort = deviceConfig["bind_serial_port"].get<std::string>();
    json portConfig = configMgr.getSerialPortConfig(serialPort);

    // 4. 初始化串口
    if (!serial.initSerial(serialPort, portConfig)) {
        return false;
    }

    isInitialized = true;
    printDeviceInfo();
    return true;
}

void SerialDirectDevice::printDeviceInfo() const {
    std::cout << "---------------- 串口直连设备信息 ----------------" << std::endl;
    std::cout << "设备ID：" << deviceId << std::endl;
    std::cout << "设备名称：" << deviceConfig["name"] << std::endl;
    std::cout << "绑定串口：" << serialPort << std::endl;
    std::cout << "从机地址：" << deviceConfig["slave_addr"] << std::endl;
    std::cout << "起始寄存器：" << deviceConfig["reg_addr"] << std::endl;
    std::cout << "读取寄存器数：" << deviceConfig["read_regs"] << std::endl;
    std::cout << "描述：" << deviceConfig["description"] << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
}