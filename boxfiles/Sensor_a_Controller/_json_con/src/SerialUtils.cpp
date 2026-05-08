#include "include/SerialUtils.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <cerrno>

SerialUtils::SerialUtils() : fd(-1), portName("") {}

SerialUtils::~SerialUtils() {
    closeSerial();
}

bool SerialUtils::configureSerial(const json& portConfig) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        std::perror("tcgetattr");
        return false;
    }

    // 配置波特率
    int baud = portConfig.value("baud_rate", 9600);
    speed_t speed = B9600;
    if (baud == 19200) speed = B19200;
    if (baud == 115200) speed = B115200;

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 配置数据位、校验位、停止位
    tty.c_cflag &= ~PARENB;    // 无校验
    tty.c_cflag &= ~CSTOPB;    // 1停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        // 8数据位

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    // 配置超时
    int timeout = portConfig.value("read_timeout_ms", 1000);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = timeout / 100;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::perror("tcsetattr");
        return false;
    }

    return true;
}

bool SerialUtils::initSerial(const std::string& portName, const json& portConfig) {
    // 关闭已有串口
    closeSerial();

    // 打开串口
    this->portName = portName;
    fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "错误：打开串口 " << portName << " 失败（" << strerror(errno) << "）" << std::endl;
        return false;
    }

    // 配置串口
    if (!configureSerial(portConfig)) {
        closeSerial();
        return false;
    }

    std::cout << "成功：串口 " << portName << " 初始化完成" << std::endl;
    return true;
}

std::vector<unsigned char> SerialUtils::hexStringsToBytes(const std::vector<std::string>& hexStrings) {
    std::vector<unsigned char> bytes;
    for (auto& s : hexStrings) {
        std::string hexStr = s;
        if (hexStr.substr(0, 2) == "0x" || hexStr.substr(0, 2) == "0X") {
            hexStr = hexStr.substr(2);
        }
        bytes.push_back(static_cast<unsigned char>(std::stoul(hexStr, nullptr, 16)));
    }
    return bytes;
}

bool SerialUtils::sendData(const std::vector<unsigned char>& data) {
    if (fd < 0) {
        std::cerr << "错误：串口未初始化" << std::endl;
        return false;
    }

    ssize_t sent = write(fd, data.data(), data.size());
    if (sent != static_cast<ssize_t>(data.size())) {
        std::perror("write");
        return false;
    }
    tcdrain(fd); // 等待发送完成

    // 打印发送的数据
    std::cout << "发送数据：";
    for (auto b : data) printf("%02X ", b);
    std::cout << std::endl;

    return true;
}

int SerialUtils::recvData(unsigned char* buf, int bufSize, int waitMs) {
    if (fd < 0) {
        std::cerr << "错误：串口未初始化" << std::endl;
        return -1;
    }

    // 等待响应
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));

    // 清空缓冲区
    memset(buf, 0, bufSize);

    // 读取数据
    int len = read(fd, buf, bufSize);
    if (len <= 0) {
        std::cout << "警告：未收到响应（超时）" << std::endl;
        return len;
    }

    // 打印接收的数据
    std::cout << "接收数据（长度：" << len << "）：";
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    std::cout << std::endl;

    return len;
}

void SerialUtils::closeSerial() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
        std::cout << "串口 " << portName << " 已关闭" << std::endl;
    }
}

int SerialUtils::getFd() const {
    return fd;
}

bool SerialUtils::isInitialized() const {
    return fd >= 0;
}