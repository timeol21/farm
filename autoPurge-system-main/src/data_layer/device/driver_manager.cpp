#include "data_layer/device/driver/driver_manager.h"
#include "common/log/log_manager.h"


std::shared_ptr<IDeviceDriver> DriverManager::getOrCreate(const InterfaceDefinition& iface){
    std::lock_guard<std::mutex> lock(mutex_);

        std::string key = iface.id; // "/dev/ttyS4"

        auto it = drivers_.find(key);
        if (it != drivers_.end()) {
            return it->second;
        }

        // ⭐ 只在这里创建一次
        auto driver = std::make_shared<ModbusRtuDriver>(iface);
        drivers_[key] = driver;
        return driver;
}


bool DriverManager::initializeAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [key, driver] : drivers_) {
        if (!driver->connect()) {
            // std::cerr << "Driver connect failed: " << key << std::endl;
            return false;
        }
    }
    return true;
}


void DriverManager::shutdownAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [key, driver] : drivers_) {
        driver->disconnect();
    }
}