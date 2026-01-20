#include "DeviceFactory.h"
#include "Sensors.h"
#include "Controllers.h"

Device* DeviceFactory::createSensor(std::string type, std::string name, std::string port) {
    if (type == "Smoke") {
        return new SmokeSensor(name, port);
    } else if (type == "Water") {
        return new WaterSensor(name, port);
    } else if (type == "Humiture") {
        return new HumitureSensor(name, port);
    }
    return nullptr;
}

Device* DeviceFactory::createController(std::string name, std::string port, int addr) {
    // 目前主要是电磁阀控制器
    return new ValveController(name, port, addr);
}