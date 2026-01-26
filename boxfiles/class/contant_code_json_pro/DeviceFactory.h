#ifndef DEVICE_FACTORY_H
#define DEVICE_FACTORY_H

#include <memory>
#include "Device.h"
#include "json.hpp"

class DeviceFactory {
public:
    // 使用智能指针 unique_ptr 接收并返回，确保所有权清晰
    static std::unique_ptr<Device> createFromJson(const nlohmann::json& j);
};

#endif