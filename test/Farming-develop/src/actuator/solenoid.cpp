#include "actuator/solenoid.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>
#include <string>

Solenoid::Solenoid(
    DeviceConfig cfg,
    DeviceState baseState
) : cfg_(cfg), baseState_(baseState) {}

bool Solenoid::init() {
    return initSerial();
}

bool Solenoid::update(){
    return querySolenoidValveStatus();
}

void Solenoid::stop() {
    closeSerial();
}

bool Solenoid::execute(const nlohmann::json& params) {
    std::string cmd = params.value("command", "");
    if(cmd == "open") {
        return openSolenoidValve();
    }else if(cmd == "close") {
        return closeSolenoidValve();
    }
    return false;
}

bool Solenoid::configureSerial(int fd, int baudRate, int dataBits, std::string parity, int stopBits)
{
    struct termios tty;

    // Get current serial port attributes
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("Failed to get serial attributes (tcgetattr)");
        return false;
    }

    //==================== Fixed core settings ====================
    // Disable hardware flow control (RTS/CTS)
    tty.c_cflag &= ~CRTSCTS;
    // Enable receiver & local mode, ignore modem control signals
    tty.c_cflag |= CREAD | CLOCAL;

    // Disable software flow control (XON/XOFF)
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Disable all input character translation and special handling
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output mode, no output processing
    tty.c_oflag = 0;

    // Disable canonical mode, echo, signal interrupt and extended functions
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

    // Read timeout setting
    tty.c_cc[VMIN] = 0;     // Minimum read bytes
    tty.c_cc[VTIME] = 10;   // Read timeout: unit 0.1s, total 1s
    //=============================================================

    // Set baud rate
    speed_t speed;
    switch (baudRate)
    {
        case 4800:    speed = B4800;    break;
        case 9600:    speed = B9600;    break;
        case 19200:   speed = B19200;   break;
        case 38400:   speed = B38400;   break;
        case 115200:  speed = B115200;  break;
        default:
            fprintf(stderr, "Unsupported baud rate: %d\n", baudRate);
            return false;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Set data bits: 5/6/7/8
    tty.c_cflag &= ~CSIZE; // Clear old data bit setting
    switch (dataBits)
    {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            fprintf(stderr, "Unsupported data bits: %d\n", dataBits);
            return false;
    }

    // Set parity check mode
    tty.c_cflag &= ~(PARENB | PARODD); // Clear old parity config
    if (parity == "N" || parity == "n")
    {
        // 无校验
    }
    else if (parity == "E" || parity == "e")
    {
        // 偶校验
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
    }
    else if (parity == "O" || parity == "o")
    {
        // 奇校验
        tty.c_cflag |= PARENB;
        tty.c_cflag |= PARODD;
    }
    else
    {
        fprintf(stderr, "Unsupported parity type: %s\n", parity.c_str());
        return false;
    }

    // Set stop bits: 1 or 2
    if (stopBits == 1)
    {
        tty.c_cflag &= ~CSTOPB; // 1 stop bit
    }
    else if (stopBits == 2)
    {
        tty.c_cflag |= CSTOPB;  // 2 stop bits
    }
    else
    {
        fprintf(stderr, "Unsupported stop bits: %d\n", stopBits);
        return false;
    }

    // Apply new serial port settings immediately
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("Failed to set serial attributes (tcsetattr)");
        return false;
    }

    return true;
}

bool Solenoid::initSerial() {
    // If already opened
    if (fd_ >= 0) {
        std::cout << "Serial port is already initialized" << std::endl;
        return true;
    }
    
    // std::cout<< portName <<std::endl;

    fd_ = open(cfg_.channelConfig.port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        perror("Failed to open serial port");
        return false;
    }

    std::cout<<" ---------------------1--------------------"<<std::endl;
    if (!configureSerial(fd_,cfg_.channelConfig.baudrate,cfg_.channelConfig.dataBits,cfg_.channelConfig.parity,cfg_.channelConfig.stopBits)) {
        std::cout<<"wrong"<<std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    std::cout << "Serial port initialized successfully (Device: " << cfg_.channelConfig.port << ")" << std::endl;
    return true;
}

void Solenoid::closeSerial() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        std::cout << "Serial port closed" << std::endl;
    }
}

// -------------------------- 电磁阀（Y1）控制函数 --------------------------
bool Solenoid::openSolenoidValve() {
    // 硬编码：打开电磁阀指令
    std::vector<uint8_t> sendCmd = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};

    // Check if serial port is ready
    if (fd_ < 0) {
        std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd_, TCIOFLUSH);

    // Send data
    ssize_t sent = write(fd_, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send command");
        return false;
    }

    // Print sent data
    std::cout << "Command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read response
    uint8_t recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        std::cout << std::endl;
    }else {
        std::cerr << "Warning: Serial port connected, but no response from solenoid valve!" << std::endl;
    }
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}

bool Solenoid::closeSolenoidValve() {
    // 硬编码：关闭电磁阀指令
    std::vector<uint8_t> sendCmd = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};

    // Check if serial port is initialized
    if (fd_ < 0) {
        std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd_, TCIOFLUSH);

    // Send command data
    ssize_t sent = write(fd_, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send close command");
        return false;
    }

    // Print sent command
    std::cout << "Close command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read response from serial port
    uint8_t recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        std::cout << std::endl;
    }else {
        std::cerr << "Warning: Serial port connected, but no response received for close command!" << std::endl;
    }
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}

// 电磁阀（Y1）状态查询
bool Solenoid::querySolenoidValveStatus() {
    // 硬编码：查询状态指令
    std::vector<uint8_t> sendCmd = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};

    // Check if serial port is initialized
    if (fd_ < 0) {
        std::cerr << "Error: Serial port not initialized, please initialize first" << std::endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd_, TCIOFLUSH);

    // Send command data
    ssize_t sent = write(fd_, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send status query command");
        return false;
    }

    // Print sent command
    std::cout << "Status query command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    std::cout << std::endl;

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read response from serial port
    uint8_t recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        std::cout << std::endl;

        // Parse status from response
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                std::cout << "Current status: Opened" << std::endl;
            } else {
                std::cout << "Current status: Closed" << std::endl;
            }
        } else {
            std::cout << "Invalid status response" << std::endl;
        }
    } else {
        std::cout << "No status response received" << std::endl;
    }
    
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}