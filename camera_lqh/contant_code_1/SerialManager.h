#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <string>
#include <iostream>
using namespace std;

class SerialManager {
public:
    int fd; // 串口文件描述符（身份证）

    SerialManager();
    ~SerialManager();

    // 简单明了的初始化：打开并设置 9600 波特率
    bool initPort(string path);
    
    // 简单的发送和接收
    void sendData(unsigned char* buf, int len);
    int recvData(unsigned char* buf);
    void closePort();
};

#endif