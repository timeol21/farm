#ifndef DOOR_LOCK_STATUS_H
#define DOOR_LOCK_STATUS_H

#include "data_layer/device/device_status.h"
#include "data_layer/gpio_device/gpio_types.h"
class DoorLockStatus : public DeviceStatus{

    public:
        DoorLockStatus(const std::string& deviceId,
                       const int type,
                       const std::string& name,
                       const LockStatus& status);
        DoorLockStatus() = default;
        ~DoorLockStatus() = default;

        bool isLock();

    private:
        LockStatus status_;

};

#endif