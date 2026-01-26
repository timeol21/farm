#include "Sensors.h"
#include <iostream>

void SmokeSensor::execute() {
    if (!sm.openPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x01, 0x00, 0x01, 0xAD, 0x3A};
    sm.sendData(cmd, 8);
    unsigned char buf[256];
    if (sm.recvData(buf, 256) >= 4 && buf[3] == 0x01)
        std::cout << deviceName << ": 检测到烟雾！" << std::endl;
}

void WaterSensor::execute() {
    if (!sm.openPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};
    sm.sendData(cmd, 8);
    unsigned char buf[256];
    if (sm.recvData(buf, 256) >= 4 && buf[3] == 0x01)
        std::cout << deviceName << ": 检测到水位异常！" << std::endl;
}

void HumitureSensor::execute() {
    if (!sm.openPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    sm.sendData(cmd, 8);
    unsigned char buf[256];
    if (sm.recvData(buf, 256) == 9) {
        int humi = (buf[3] << 8) | buf[4];
        int temp = (buf[5] << 8) | buf[6];
        std::cout << deviceName << ": 温度 " << temp/10.0 << "C, 湿度 " << humi/10.0 << "%" << std::endl;
    }
}