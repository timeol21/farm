#include "Solenoid.h"
#include <iostream>                   
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>


Solenoid::Solenoid(
    int id,
    const std::string& deviceId,
    const std::string& deviceState,
    const std::string& portName,
    int plcId,
    int portState,
    const std::string& solenoidName,
    int solenoidId     
) : PLCDevice(id, deviceId, deviceState, portName, plcId, portState), solenoidName_(solenoidName), solenoidId_(solenoidId) {}



bool Solenoid::openSolenoid(const std::vector<uint8_t>& sendCmd) {


    // 电磁阀（Y1）开启指令（地址0x0501，校验码重新计算）
    // unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};

    int fd = getPortState();
    // if (fd < 0) {
    //     std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
    //     return false;
    // }
    if (fd < 0) {
        std::cout << "Initializing serial port...\n";
        if(!plcInitSerial()){
            std::cerr << "Serial port initialization failed!\n";
            return false;
        }
        fd = getPortState();
    }
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());

    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send open command");
        return false;
    }

    std::cout << "Open command sent: ";
    for (size_t i = 0; i < sendCmd.size(); ++i)
        printf("%02X ", sendCmd[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;
    }

    std::cout << "Solenoid valve: Opened" << std::endl;
    return true;    
}

bool Solenoid::closeSolenoid(const std::vector<uint8_t>& sendCmd){
    int fd = PLCDevice::getPortState();
    if (fd < 0) {
        std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
        return false;
    }

    // 电磁阀（Y1）关闭指令（地址0x0501，校验码重新计算）
    // unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());

    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send close command");
        return false;
    }

    std::cout << "Close command sent: ";
    for (size_t i = 0; i < sendCmd.size(); ++i)
        printf("%02X ", sendCmd[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;
    }

    std::cout << "Solenoid valve: Closed" << std::endl;
    return true;    
}

bool Solenoid::querySolenoid(const std::vector<uint8_t>& sendCmd){
    int fd = PLCDevice::getPortState();
    if (fd < 0) {
        std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
        return false;
    }

    // 电磁阀（Y1）查询指令（功能码0x01，查询Y1线圈状态，地址0x0501）
    // unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());

    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send status query command");
        return false;
    }

    std::cout << "Status query command sent: ";
    for (size_t i = 0; i < sendCmd.size(); ++i)
        printf("%02X ", sendCmd[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;

        // 解析响应：第3字节为数据长度，第4字节bit0表示状态（1=开启，0=关闭）
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                std::cout << "Current status: Opened" << std::endl;
            } else {
                std::cout << "Current status: Closed" << std::endl;
            }
        } else {
            std::cout << "Invalid status response" << std::endl;
        }
    } else {
        std::cout << "No status response received" << std::endl;
    }

    return true;    
}

void Solenoid::openCurrentSolenoid() {
    std::vector<uint8_t> cmd = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
    openSolenoid(cmd);
}
void Solenoid::queryCurrentSolenoid() {
    std::vector<uint8_t> cmd = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    querySolenoid(cmd);
}

void Solenoid::closeCurrentSolenoid() {
    std::vector<uint8_t> cmd = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    closeSolenoid(cmd);
}

