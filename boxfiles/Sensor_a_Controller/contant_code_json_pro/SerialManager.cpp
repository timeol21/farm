#include "SerialManager.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>

SerialManager::~SerialManager() {
    closePort();
}

bool SerialManager::initPort(const std::string& path) {
    if (fd >= 0 && portPath == path) return true;
    if (fd >= 0) closePort();

    portPath = path;
    fd = open(portPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror(("Error opening " + portPath).c_str());
        return false;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) return false;

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; 

    tcsetattr(fd, TCSANOW, &tty);
    return true;
}

void SerialManager::sendData(const unsigned char* buf, int len) {
    if (fd >= 0) write(fd, buf, len);
}

int SerialManager::recvData(unsigned char* buf) {
    return (fd >= 0) ? read(fd, buf, 255) : -1;
}

void SerialManager::closePort() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}