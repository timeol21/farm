#ifndef GPIO_DEVICE_H
#define GPIO_DEVICE_H

#include "data_layer/device/device.h"
class GPIODevice : public Device {

    protected:
        GPIODevice(int type,
                   const std::string& deviceId,
                   const std::string& name,
                   int gpioGroup,
                   int gpioPinNum,
                   int pin,
                   const std::string& chipName,
                   const std::string& direction)
                : Device(type,deviceId,name), 
                  gpioGroup_(gpioGroup),
                  gpioPinNum_(gpioPinNum),
                  pin_(pin),  
                  chipName_(chipName),
                  direction_(direction) {}
    public:
        virtual ~GPIODevice() = default;    

        int getPin() { return pin_; }
        std::string getDirection() { return direction_; }
    
    private:
        int gpioGroup_;
        int gpioPinNum_;
        int pin_;
        std::string chipName_;
        std::string direction_;
};

#endif