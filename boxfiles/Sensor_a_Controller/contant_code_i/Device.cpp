#include "Device.h"

Device::Device(std::string name, std::string port) 
    : deviceName(name), portPath(port) {}

Device::~Device() {}