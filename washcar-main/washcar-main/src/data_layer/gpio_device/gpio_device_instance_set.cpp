#include "data_layer/gpio_device/gpio_device_instance_set.h"

GPIODeviceInstanceSet::GPIODeviceInstanceSet(std::vector<DoorLock> doorLocks) {
    doorLocks_.reserve(doorLocks.size());

    for (auto& lock : doorLocks) {
        doorLocks_.emplace(lock.getDeviceId(), std::move(lock));
    }
}

bool GPIODeviceInstanceSet::lockDoorLock(const GPIODeviceSimpleInfo& info) {
    auto it = doorLocks_.find(info.getDeviceId());

    if(it == doorLocks_.end())
        return false;

    DoorLock& lock = it -> second;

    return lock.lockDoorLock();
}
bool GPIODeviceInstanceSet::unlockDoorLock(const GPIODeviceSimpleInfo& info) {
    auto it = doorLocks_.find(info.getDeviceId());

    if(it == doorLocks_.end())
        return false;

    DoorLock& lock = it -> second;

    return lock.unlockDoorLock();
}

std::vector<DoorLockStatus> GPIODeviceInstanceSet::getDoorLockStatusList() {
    std::vector<DoorLockStatus> doorLockStatusList;
    doorLockStatusList.reserve(doorLocks_.size());
    for(auto& [key,lock] : doorLocks_) {
        doorLockStatusList.push_back(lock.queryDoorLockStatus());
    }
    return doorLockStatusList;
}

std::vector<DeviceData> GPIODeviceInstanceSet::acquisitionDoorLockData() {
    std::vector<DeviceData> doorLockDataList;
    doorLockDataList.reserve(doorLocks_.size());

    for(auto& [key, doorLock] : doorLocks_) {
        DoorLockStatus status = doorLock.queryDoorLockStatus();
        doorLockDataList.push_back( DeviceData(3,status) );
    }

    return doorLockDataList;
}