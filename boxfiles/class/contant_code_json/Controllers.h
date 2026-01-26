#ifndef CONTROLLERS_H
#define CONTROLLERS_H

#include "Device.h"
#include "SerialManager.h"

class ValveController : public Device {
public:
    int valveAddr; // 0x0500 或 0x0501
    SerialManager sm; // 每个阀门自带一个串口管家

    ValveController();
    
    // 你习惯的额外赋值方法
    void setAddress(int addr);

    // 实现工厂要求的 execute
    void execute() override;
};

#endif