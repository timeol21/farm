#include "data_layer/gpio_device/door_lock_status.h"

DoorLockStatus::DoorLockStatus(const std::string& deviceId,
                       const int type,
                       const std::string& name,
                       const LockStatus& status)
                : DeviceStatus(deviceId,type,name),
                  status_(status) {

}

bool DoorLockStatus::isLock() {
  return status_ == LockStatus::LOCK;
}