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

#include "Solenoid.h"
#include "PLCDevice.h"



PLCDevice::PLCDevice(
    int id,
    const std::string& deviceId,
    const std::string& deviceState,
    const std::string& portName,
    int portState,
    int plcId
) : Device(id, deviceId, deviceState), portName_(portName), portState_(portState), plcId_(plcId) {}


bool PLCDevice::configSerial(int fd){
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("Failed to get serial attributes (tcgetattr)");
        return false;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Failed to set serial attributes (tcsetattr)");
        return false;
    }
    return true;    
}

bool PLCDevice::initSerial(const std::string& portName){
    if (portState_ >= 0) {
        std::cout << "Serial port is already initialized" << std::endl;
        return true;
    }

    portState_ = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (portState_ < 0) {
        perror("Failed to open serial port");
        return false;
    }

    if (!PLCDevice::configSerial(portState_)) {
        close(portState_);
        portState_ = -1;
        return false;
    }

    std::cout << "Serial port initialized successfully (Device: " << portName << ")" << std::endl;
    return true;
}

bool PLCDevice::closeSerial() {
    if (portState_ >= 0) {
        close(portState_);
        portState_ = -1;
        std::cout << "串口已关闭" << std::endl;
        return true;
    }else {
        return false;
    }
}

bool PLCDevice::plcInitSerial(){
    return initSerial(portName_);
};

bool PLCDevice::plcCloseSerial(){
    return closeSerial();
}

void PLCDevice::addSolenoid(std::shared_ptr<Solenoid> solenoid){
    int id = solenoid->getSolenoidId();
    solenoidSet_[id] = solenoid;
};

std::shared_ptr<Solenoid> PLCDevice::getSolenoid(int solenoidId) {
    auto it = solenoidSet_.find(solenoidId);
    if (it != solenoidSet_.end()) {
        return it->second;
    }
    return nullptr;
}