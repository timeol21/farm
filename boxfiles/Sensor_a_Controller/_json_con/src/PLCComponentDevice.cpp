#include "PLCComponentDevice.h"
#include <iostream>

PLCComponentDevice::PLCComponentDevice(const std::string& deviceId) 
    : BaseDevice(deviceId), plcId(""), serialPort("") {}

bool PLCComponentDevice::init() {
    // 1. 加载配置
    if (!configMgr.loadConfig()) {
        return false;
    }

    // 2. 查找PLC组件配置
    if (!configMgr.findPLCComponentConfig(deviceId, plcId, plcConfig, deviceConfig)) {
        return false;
    }

    // 3. 获取串口配置
    serialPort = plcConfig["bind_serial_port"].get<std::string>();
    json portConfig = configMgr.getSerialPortConfig(serialPort);

    // 4. 初始化串口
    if (!serial.initSerial(serialPort, portConfig)) {
        return false;
    }

    isInitialized = true;
    printDeviceInfo();
    return true;
}

void PLCComponentDevice::printDeviceInfo() const {
    std::cout << "---------------- PLC组件设备信息 ----------------" << std::endl;
    std::cout << "设备ID：" << deviceId << std::endl;
    std::cout << "设备名称：" << deviceConfig["name"] << std::endl;
    std::cout << "所属PLC：" << plcId << std::endl;
    std::cout << "绑定串口：" << serialPort << std::endl;
    std::cout << "PLC端口：" << deviceConfig["plc_port"] << std::endl;
    std::cout << "MODBUS地址：" << deviceConfig["reg_addr"] << std::endl;
    std::cout << "PLC描述：" << plcConfig["description"] << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
}