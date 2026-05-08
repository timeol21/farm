#ifndef SENSORS_H
#define SENSORS_H

#include "Device.h"
#include "SerialManager.h"

class SmokeSensor : public Device {
private:
    SerialManager sm;
public:
    SmokeSensor(const std::string& _id, const std::string& _name, const std::string& _port)
        : Device(_id, "Smoke", _name, _port) {}
    void execute() override;
};

class WaterSensor : public Device {
private:
    SerialManager sm;
public:
    WaterSensor(const std::string& _id, const std::string& _name, const std::string& _port)
        : Device(_id, "Water", _name, _port) {}
    void execute() override;
};

class HumitureSensor : public Device {
private:
    SerialManager sm;
public:
    HumitureSensor(const std::string& _id, const std::string& _name, const std::string& _port)
        : Device(_id, "Humiture", _name, _port) {}
    void execute() override;
};

#endif