#ifndef GPIO_OUTPUT_DEVICE_H
#define GPIO_OUTPUT_DEVICE_H

#include "data_layer/device/gpio_device.h"
class GPIOOutputDevice : public GPIODevice {

    protected:
        GPIOOutputDevice(int type,
                         const std::string& deviceId,
                         const std::string& name,
                         int gpioGroup,
                         int gpioPinNum,
                         int pin,
                         const std::string& chipName,
                         const std::string& direction,
                         int initialValue,
                         const std::string& activeLogic)
                    : GPIODevice(type,deviceId,name,gpioGroup,gpioPinNum,pin,chipName,direction),
                      initialValue_(initialValue),
                      activeLogic_(activeLogic) {}
    
    public:
        virtual ~GPIOOutputDevice() = default;
    
    private:
        int initialValue_;
        std::string activeLogic_;
};

#endif