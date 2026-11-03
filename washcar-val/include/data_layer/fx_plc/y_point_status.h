#pragma once
#include "data_layer/device/device_status.h"

class YPointStatus : public DeviceStatus {
public:
    YPointStatus(const std::string& deviceId, int type, const std::string& name, bool state);
    bool isOn() const { return state_; }
private:
    bool state_;
};

