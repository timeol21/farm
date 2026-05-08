#ifndef DOOR_LOCK_H
#define DOOR_LOCK_H

#include "GPIODevice.h"

class DoorLock : public GPIODevice {
public:
    DoorLock(const std::string& deviceId = "door_lock_1");
    ~DoorLock() override = default;

    // 重写打印设备信息（补充门锁特有信息）
    void printDeviceInfo() const override;
};

#endif // DOOR_LOCK_H