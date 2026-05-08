#include "SerialChannel.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iostream>

namespace BoxSystem {

SerialChannel::~SerialChannel() {
    close();
}

bool SerialChannel::open(const std::string& info) {
    // 此时 info 的格式由管理器传入，例如 "/dev/ttyS4:9600"
    size_t pos = info.find(':');
    portPath = info.substr(0, pos);
    int baud = std::stoi(info.substr(pos + 1));

    fd = ::open(portPath.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "无法打开串口: " << portPath << std::endl;
        return false;
    }

    // 串口属性配置
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return false;

    speed_t speed = (baud == 115200) ? B115200 : B9600;
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; 
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_lflag = 0; 
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // 1秒超时

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

void SerialChannel::close() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

int SerialChannel::send(const unsigned char* data, int len) {
    return (fd >= 0) ? ::write(fd, data, len) : -1;
}

int SerialChannel::receive(unsigned char* buffer, int maxLen) {
    return (fd >= 0) ? ::read(fd, buffer, maxLen) : -1;
}

} // namespace BoxSystem