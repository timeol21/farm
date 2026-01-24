#include "Controllers.h"

ValveController::ValveController() {
    valveAddr = 0x0500; // 默认给个地址
}

void ValveController::setAddress(int addr) {
    valveAddr = addr;
}

void ValveController::execute() {
    // 1. 如果没通电（串口没开），就先通电
    if (sm.fd < 0) {
        sm.initPort(portPath);
    }

    cout << "--- 执行控制: " << type << " (ID: " << id << ") ---" << endl;

    // 2. 根据地址发送对应的写死报文
    if (valveAddr == 0x0500) {
        unsigned char buf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
        sm.sendData(buf, 8);
    } else {
        unsigned char buf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
        sm.sendData(buf, 8);
    }
    
    cout << "指令已通过 " << portPath << " 发送完毕。" << endl;
}