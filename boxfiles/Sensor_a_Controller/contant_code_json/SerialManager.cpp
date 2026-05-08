#include "SerialManager.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdio>

SerialManager::SerialManager() : fd(-1) {}

SerialManager::~SerialManager() {
    closePort();
}

bool SerialManager::initPort(string path) {
    if (fd >= 0) return true; // 已经打开就不用再开了

    fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        cout << "错误: 无法打开串口 " << path << endl;
        return false;
    }

    // 简化的串口配置（小白不用深究，知道是配9600就行）
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return false;
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | CSTOPB);
    tcsetattr(fd, TCSANOW, &tty);
    
    return true;
}

void SerialManager::sendData(unsigned char* buf, int len) {
    if (fd >= 0) write(fd, buf, len);
}

int SerialManager::recvData(unsigned char* buf) {
    if (fd < 0) return 0;
    return read(fd, buf, 255); // 最多读255字节
}

void SerialManager::closePort() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}