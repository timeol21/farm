#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <string>

class SerialManager {
public:
    int fd;

    SerialManager();
    ~SerialManager();

    bool openPort(std::string path);
    void sendData(unsigned char* buf, int len);
    int recvData(unsigned char* buf, int maxLen);
    void closePort();
};

#endif