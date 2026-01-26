#include "Sensors.h"
#include <thread>
#include <chrono>

void SmokeSensor::execute() {
    if (!sm.initPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x01, 0x00, 0x01, 0xAD, 0x3A};
    sm.sendData(cmd, 8);
    unsigned char buf[255];
    int len = sm.recvData(buf);
    std::cout << "[" << name << "] Smoke Status: " << ((len >= 4 && buf[3] == 0x01) ? "ALARM!" : "Normal") << std::endl;
}

void WaterSensor::execute() {
    if (!sm.initPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};
    sm.sendData(cmd, 8);
    unsigned char buf[255];
    int len = sm.recvData(buf);
    std::cout << "[" << name << "] Water Status: " << ((len >= 4 && buf[3] == 0x01) ? "OVERFLOW!" : "Normal") << std::endl;
}

void HumitureSensor::execute() {
    if (!sm.initPort(portPath)) return;
    unsigned char cmd[] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x95, 0xCB}; // 修正后的0001地址
    sm.sendData(cmd, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    unsigned char buf[255];
    int len = sm.recvData(buf);
    if (len == 9) {
        float humi = ((buf[3] << 8) | buf[4]) / 10.0;
        float temp = ((buf[5] << 8) | buf[6]) / 10.0;
        std::cout << "[" << name << "] Temp: " << temp << "C, Humi: " << humi << "%" << std::endl;
    }
}