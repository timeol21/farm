#include "SerialManager.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdio>

SerialManager::SerialManager() : fd(-1) {}

SerialManager::~SerialManager() {
    closePort();
}

bool SerialManager::openPort(std::string path) {
    if (fd >= 0) return true; // 已经打开了

    fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("串口打开失败");
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return false;

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

void SerialManager::sendData(unsigned char* buf, int len) {
    if (fd >= 0) write(fd, buf, len);
}

int SerialManager::recvData(unsigned char* buf, int maxLen) {
    if (fd < 0) return 0;
    return read(fd, buf, maxLen);
}

void SerialManager::closePort() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}