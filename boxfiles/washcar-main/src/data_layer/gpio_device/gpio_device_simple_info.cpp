#include "data_layer/gpio_device/gpio_device_simple_info.h"

GPIODeviceSimpleInfo::GPIODeviceSimpleInfo(const std::string& deviceId,const std::string& name)
                        : deviceId_(deviceId),name_(name){

}

std::string GPIODeviceSimpleInfo::getDeviceId() const {
    return deviceId_;
}