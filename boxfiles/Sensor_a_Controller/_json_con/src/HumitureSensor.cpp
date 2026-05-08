#include "HumitureSensor.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstring>

HumitureSensor::HumitureSensor(const std::string& deviceId) 
    : SerialDirectDevice(deviceId) {}

bool HumitureSensor::readData() {
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

    // 4. 解析温湿度
    int slaveAddr = std::stoul(deviceConfig["slave_addr"].get<std::string>().substr(2), nullptr, 16);
    if (len == 9 && recvBuf[0] == slaveAddr && recvBuf[1] == 0x03) {
        uint16_t humiData = (recvBuf[3] << 8) | recvBuf[4];
        uint16_t tempData = (recvBuf[5] << 8) | recvBuf[6];
        float temperature = tempData / 10.0;
        float humidity = humiData / 10.0;

        std::cout << "解析成功！" << std::endl;
        std::cout << "温度：" << std::fixed << std::setprecision(1) << temperature << "℃ " 
                  << "湿度：" << std::fixed << std::setprecision(1) << humidity << "%RH" << std::endl;
        return true;
    } else if (len == 0) {
        std::cout << "温湿度传感器：未收到响应" << std::endl;
    } else {
        std::cout << "温湿度传感器：响应异常" << std::endl;
    }

    return false;
}