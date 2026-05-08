#include "Valve.h"
#include "Init.h"       
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdio>

// 封装fd获取逻辑
int Valve::getSerialFd() const {
    // 公共接口获取fd
    return InitSerial::GetFdByPlcId(this->plcId);
}

// 开启电磁阀
bool Valve::OpenSolenoidValve() {
    int fd = getSerialFd();
    if (fd < 0) {
        cerr << "【PLC" << plcId << "-电磁阀】错误：无有效串口fd，操作失败！" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
    ssize_t sent = write(fd, sendBuf, sizeof(sendBuf));
    if (sent != (ssize_t)sizeof(sendBuf)) {
        cerr << "【PLC" << plcId << "-电磁阀】开启指令发送失败：" << strerror(errno) << endl;
        return false;
    }

    cout << "【PLC" << plcId << "-电磁阀】开启指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "【PLC" << plcId << "-电磁阀】接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "【PLC" << plcId << "-电磁阀】状态：已开启" << endl;
    return true;
}

// 关闭电磁阀
bool Valve::CloseSolenoidValve() {
    int fd = getSerialFd();
    if (fd < 0) {
        cerr << "【PLC" << plcId << "-电磁阀】错误：无有效串口fd，操作失败！" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    ssize_t sent = write(fd, sendBuf, sizeof(sendBuf));
    if (sent != (ssize_t)sizeof(sendBuf)) {
        cerr << "【PLC" << plcId << "-电磁阀】关闭指令发送失败：";
        return false;
    }

    cout << "【PLC" << plcId << "-电磁阀】关闭指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "【PLC" << plcId << "-电磁阀】接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "【PLC" << plcId << "-电磁阀】状态：已关闭" << endl;
    return true;
}

// 查询电磁阀状态
bool Valve::QuerySolenoidValveStatus() {
    int fd = getSerialFd();
    if (fd < 0) {
        cerr << "【PLC" << plcId << "-电磁阀】错误：无有效串口fd，操作失败！" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    ssize_t sent = write(fd, sendBuf, sizeof(sendBuf));
    if (sent != (ssize_t)sizeof(sendBuf)) {
        cerr << "[PLC" << plcId << "-电磁阀]状态查询指令发送失败：" << strerror(errno) << endl;
        return false;
    }

    cout << "【PLC" << plcId << "-电磁阀】状态查询指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "【PLC" << plcId << "-电磁阀】接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                cout << "【PLC" << plcId << "-电磁阀】当前状态：开启" << endl;
            } else {
                cout << "【PLC" << plcId << "-电磁阀】当前状态：关闭" << endl;
            }
        } else {
            cout << "【PLC" << plcId << "-电磁阀】状态查询响应无效" << endl;
        }
    } else {
        cout << "【PLC" << plcId << "-电磁阀】未接收到状态响应" << endl;
    }

    return true;
}