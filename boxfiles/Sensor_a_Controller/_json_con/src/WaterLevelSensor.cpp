#include "WaterLevelSensor.h"
#include <iostream>
#include <vector>
#include <cstring>

WaterLevelSensor::WaterLevelSensor(const std::string& deviceId) 
    : PLCComponentDevice(deviceId) {}

bool WaterLevelSensor::readData() {
    if (!serial.isInitialized()) {
        std::cerr << "错误：串口未初始化" << std::endl;
        return false;
    }

    // 1. 构建查询指令
    auto cmdStrings = deviceConfig["commands"]["query"].get<std::vector<std::string>>();
    std::vector<unsigned char> sendBuf = SerialUtils::hexStringsToBytes(cmdStrings);

    // 2. 发送指令
    if (!serial.sendData(sendBuf)) {
        return false;
    }

    // 3. 接收响应
    unsigned char recvBuf[256];
    int len = serial.recvData(recvBuf, sizeof(recvBuf), 100);

    // 4. 解析响应
    if (len >= 4 && recvBuf[1] == 0x01) {
        int state = recvBuf[3];
        std::cout << "水位传感器状态：";
        if (state == 0) {
            std::cout << "低电平（未触发，水位正常）" << std::endl;
        } else if (state == 1) {
            std::cout << "高电平（触发，水位异常）" << std::endl;
        } else {
            std::cout << "未知状态（值：" << state << "）" << std::endl;
        }
        return true;
    } else {
        std::cout << "水位传感器：响应格式无效" << std::endl;
        return false;
    }
}