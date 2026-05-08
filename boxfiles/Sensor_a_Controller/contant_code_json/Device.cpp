#include "Device.h"

Device::Device() {
    id = "DefaultID";
    type = "UnknownType";
    portPath = "/dev/ttyS4";
}

Device::~Device() {
    // 基础析构
}

void Device::setBaseInfo(string newId, string newType, string newPort) {
    id = newId;
    type = newType;
    portPath = newPort;
}