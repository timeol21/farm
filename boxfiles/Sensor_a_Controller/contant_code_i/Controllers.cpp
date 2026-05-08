#include "Controllers.h"
#include <iostream>

ValveController::ValveController(std::string name, std::string port, int addr)
    : Device(name, port), valveAddress(addr) {}

void ValveController::execute() {
    if (!sm.openPort(portPath)) return;

    if (valveAddress == 0x0500) {
        unsigned char buf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
        sm.sendData(buf, 8);
    } else {
        unsigned char buf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
        sm.sendData(buf, 8);
    }
    std::cout << "控制器 [" << deviceName << "] 已发送指令" << std::endl;
}