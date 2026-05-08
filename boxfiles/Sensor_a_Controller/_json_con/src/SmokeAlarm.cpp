#include "SmokeAlarm.h"
#include <iostream>
#include <vector>
#include <cstring>

SmokeAlarm::SmokeAlarm(const std::string& deviceId) 
    : PLCComponentDevice(deviceId) {}

bool SmokeAlarm::readData() {
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
    int len = serial.recvData(recvBuf, sizeof(recvBuf), 200);

    // 4. 解析响应
    if (len >= 4 && recvBuf[1] == 0x01) {
        int state = recvBuf[3];
        if (state == 0) {
            std::cout << "⚠️  烟感报警！！！" << std::endl;
        } else {
            std::cout << "烟感状态：正常" << std::endl;
        }
        return true;
    } else {
        std::cout << "烟感传感器：响应格式无效" << std::endl;
        return false;
    }
}