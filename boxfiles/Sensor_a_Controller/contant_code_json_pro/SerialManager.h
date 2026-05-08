#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <string>

class SerialManager {
private:
    int fd = -1;
    std::string portPath;

public:
    SerialManager() = default;
    ~SerialManager(); // 析构时自动关串口

    bool initPort(const std::string& path);
    void sendData(const unsigned char* buf, int len);
    int recvData(unsigned char* buf);
    void closePort();
};

#endif