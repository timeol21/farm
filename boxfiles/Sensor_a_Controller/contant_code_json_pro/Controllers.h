#ifndef CONTROLLERS_H
#define CONTROLLERS_H

#include "Device.h"
#include "SerialManager.h"

class ValveController : public Device {
private:
    SerialManager sm;
    int addr;
public:
    ValveController(const std::string& _id, const std::string& _name, const std::string& _port, int _addr)
        : Device(_id, "Valve", _name, _port), addr(_addr) {}
    void execute() override;
};

#endif