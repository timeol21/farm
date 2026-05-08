#include "SerialChannel.h"
#include <fcntl.h>
#include <termios.h>

namespace BoxSystem {
SerialChannel::SerialChannel(const std::string& port, int baudRate) : fd(-1), portName(port) {}
SerialChannel::~SerialChannel() { close(); }

bool SerialChannel::open() {
    fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) return false;
    struct termios opt;
    tcgetattr(fd, &opt);
    cfsetispeed(&opt, B9600);
    cfsetospeed(&opt, B9600);
    tcsetattr(fd, TCSANOW, &opt);
    return true;
}

void SerialChannel::close() { if(fd >= 0) ::close(fd); fd = -1; }

int SerialChannel::send(const unsigned char* data, int len) {
    std::lock_guard<std::mutex> lock(mtx);
    return (fd < 0) ? -1 : ::write(fd, data, len);
}

int SerialChannel::receive(unsigned char* buffer, int maxLen) {
    std::lock_guard<std::mutex> lock(mtx);
    return (fd < 0) ? -1 : ::read(fd, buffer, maxLen);
}
}