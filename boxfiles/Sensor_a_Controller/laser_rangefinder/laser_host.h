#ifndef LASER_HOST_H
#define LASER_HOST_H

#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

class LaserHost {
public:
    explicit LaserHost(const char* port = "/dev/ttyACM0");           //explicit禁止使用变量不同时的自动转化
    bool init();
    void deinit();

    // ====================== 基础命令 ======================
    int sendBaudMatch();
    int stopContinuousMeasure(); // 发送 0X58 停止连续测量

    // ====================== 单次测量 ======================
    int singleMeasureFast(uint8_t addr);
    int singleMeasureSlow(uint8_t addr);
    int singleMeasureAuto(uint8_t addr);

    // ====================== 连续测量 ======================
    int continuousMeasureFast(uint8_t addr);
    int continuousMeasureSlow(uint8_t addr);
    int continuousMeasureAuto(uint8_t addr);

    // ====================== 参数设置 ======================
    int setSlaveAddress(uint8_t old_addr, uint8_t new_addr);
    int setMeasureOffset(uint8_t addr, int offset_mm);
    int broadcastAllMeasure();

    // ====================== 激光控制 ======================
    int laserOn(uint8_t addr);
    int laserOff(uint8_t addr);
    void allLaserOn(const std::vector<uint8_t>& addrs);
    void allLaserOff(const std::vector<uint8_t>& addrs);

    // ====================== 公共读取解析 ======================
    int readResponse(uint8_t* buffer, int timeout_ms);     //读取原始报文
    bool parseDistance(const uint8_t* response, int len, int& distance_mm);  //解析报文，得出距离

private:
    std::string m_port;
    int m_fd;
    const int m_baudrate = B115200;

    bool grantPermission();
    int openSerial();
    int configSerial(int fd);

    // 通用发送接口
    int sendPacket(const uint8_t* pkt, int len, const char* name);
};

#endif