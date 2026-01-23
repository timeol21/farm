#ifndef CONTROLLERS_H
#define CONTROLLERS_H

#include "Device.h"
#include "SerialManager.h"

class ValveController : public Device {
private:
    int valveAddress;
    SerialManager sm;

public:
    ValveController(std::string name, std::string port, int addr);
    void execute() override;
};

#endif