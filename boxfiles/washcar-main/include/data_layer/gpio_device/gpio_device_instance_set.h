#ifndef GPIO_DEVICE_INSTANCE_SET_H
#define GPIO_DEVICE_INSTANCE_SET_H

#include <vector>
#include <unordered_map>
#include "data_layer/gpio_device/door_lock_status.h"
#include "data_layer/gpio_device/gpio_device_simple_info.h"
#include "data_layer/gpio_device/door_lock.h"
#include "data_layer/device/device_data.h"
class GPIODeviceInstanceSet {

    public:
        GPIODeviceInstanceSet() = default;
        GPIODeviceInstanceSet(std::vector<DoorLock> doorLocks);
        ~GPIODeviceInstanceSet() = default;

        bool lockDoorLock(const GPIODeviceSimpleInfo& info);
        bool unlockDoorLock(const GPIODeviceSimpleInfo& info);

        std::vector<DoorLockStatus> getDoorLockStatusList();

        std::vector<DeviceData> acquisitionDoorLockData();
    
    private:
        std::unordered_map<std::string, DoorLock> doorLocks_;
};

#endif