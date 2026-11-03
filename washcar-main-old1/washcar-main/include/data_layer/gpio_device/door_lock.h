#ifndef DOOR_LOCK_H
#define DOOR_LOCK_H

#include "data_layer/device/gpio_output_device.h"
#include "data_layer/gpio_device/door_lock_status.h"
class DoorLock : public GPIOOutputDevice {

    public:
        DoorLock() = default;
        DoorLock(int type,
                 const std::string& deviceId,
                 const std::string& name,
                 int gpioGroup,
                 int gpioPinNum,
                 int pin,
                 const std::string& chipName,
                 const std::string& direction,
                 int initialValue,
                 const std::string& activeLogic);
        ~DoorLock() override = default;
    
        bool lockDoorLock();
        bool unlockDoorLock();

        std::unique_ptr<DeviceStatus> getStatus() const ;

        DoorLockStatus queryDoorLockStatus();
    
    private:
        bool isGPIOExport();
        bool exportGPIO();
        bool setGPIODirection();
        std::string getGPIOSysPath() ;
};

#endif