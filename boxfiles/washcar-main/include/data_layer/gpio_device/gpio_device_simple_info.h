#ifndef GPIO_DEVICE_SIMPLE_INFO_H
#define GPIO_DEVICE_SIMPLE_INFO_H

#include <string>
class GPIODeviceSimpleInfo {

    public:
        GPIODeviceSimpleInfo() = default;
        GPIODeviceSimpleInfo(const std::string& deviceId,const std::string& name);
        ~GPIODeviceSimpleInfo() = default;
    
        std::string getDeviceId() const;
    private:
        std::string deviceId_;
        std::string name_;
};

#endif