#include "data_layer/car/car_control.h"

CarControl::CarControl(const std::string& deviceId)  : deviceId_(deviceId) {

}

const std::string& CarControl::getDeviceId() const {
    return deviceId_;
} 