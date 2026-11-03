#include "data_layer/gpio_device/door_lock.h"

#include <sys/stat.h>
#include <fstream>
DoorLock::DoorLock(int type,
                   const std::string& deviceId,
                   const std::string& name,
                   int gpioGroup,
                   int gpioPinNum,
                   int pin,
                   const std::string& chipName,
                   const std::string& direction,
                   int initialValue,
                   const std::string& activeLogic)
            : GPIOOutputDevice(type,deviceId,name,gpioGroup,gpioPinNum,
                               pin,chipName,direction,initialValue,activeLogic){

}

bool DoorLock::lockDoorLock() {
    std::ofstream valueFile(getGPIOSysPath());
    if (!valueFile.is_open()) {
        //无法打开value文件（权限不足）
        return false;
    }
    valueFile << 1;
    valueFile.close();
    return true;
}

bool DoorLock::unlockDoorLock() {
    std::ofstream valueFile(getGPIOSysPath());
    if (!valueFile.is_open()) {
        //无法打开value文件（权限不足）
        return false;
    }
    valueFile << 0;
    valueFile.close();
    return true;
}

std::unique_ptr<DeviceStatus> DoorLock::getStatus() const {
    return std::make_unique<DeviceStatus> ();
}

DoorLockStatus DoorLock::queryDoorLockStatus() {
    std::ifstream valueFile(getGPIOSysPath());
    if (!valueFile.is_open()) {
        //无法读取value文件"
        return DoorLockStatus(this->getDeviceId(),4,this->getName(),LockStatus::UNKNOW);
    }
    std::string valueStr;
    valueFile >> valueStr;
    valueFile.close();
    if (valueStr == "1") return  DoorLockStatus(this->getDeviceId(),4,this->getName(),LockStatus::UNLOCK);
    return DoorLockStatus(this->getDeviceId(),4,this->getName(),LockStatus::LOCK);
}

bool DoorLock::isGPIOExport() {
    struct stat st;
    return stat(getGPIOSysPath().c_str(),&st) == 0;
}
bool DoorLock::exportGPIO() {
    std::ofstream exportFile("/sys/class/gpio/export");
    if(!exportFile.is_open()) {
        //无法打开export文件，权限不足或系统不支持
        return false;
    }
    exportFile << this->getPin();
    exportFile.close();
    return true;
}
bool DoorLock::setGPIODirection() {
    if ( this->getDirection() != "in" && this->getDirection() != "out") {
        //方向参数必须是\"in\"或\"out\""
        return false;
    }

    std::string dirPath = getGPIOSysPath() + "/direction";
    std::ofstream dirFile(dirPath);
    if (!dirFile.is_open()) {
        //无法打开direction文件
        return false;
    }
    dirFile << this->getDirection();
    dirFile.close();
    return true;
}

std::string DoorLock::getGPIOSysPath() {
    return "/sys/class/gpio/gpio" + std::to_string(this->getPin());
}