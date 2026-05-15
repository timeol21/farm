#include"Device.h"

Device::Device(
    int id,
    const std::string& deviceId,
    const std::string& deviceState
) :id_(id), deviceId_(deviceId), deviceState_(deviceState){}

int Device::getId() const {return id_;}
std::string Device::getDeviceId() const {return deviceId_;}
std::string Device::getDeviceState() const {return deviceState_;}
