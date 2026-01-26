#include "DeviceFactory.h"
#include "Sensors.h"
#include "Controllers.h"

Device* DeviceFactory::createDevice(std::string kind, std::string id, std::string name, std::string port, int addr) {
    Device* dev = nullptr; 

    // 1. 根据类型 new 出对应的对象
    if (kind == "Smoke") dev = new SmokeSensor();
    else if (kind == "Water") dev = new WaterSensor();
    else if (kind == "Humiture") dev = new HumitureSensor();
    else if (kind == "Valve") {
        ValveController* v = new ValveController();
        v->setAddress(addr); // 特有的地址设置
        dev = v;
    }

    // 2. 进行赋值
    if (dev != nullptr) {
        dev->setBaseInfo(id, name, port);
    }

    return dev;
}