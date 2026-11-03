#pragma once

#include <memory>
#include "i_device.h"
#include "utils/json.hpp"

class IProtocol;

class DeviceFactory {
public:
	static std::unique_ptr<IDevice> createDevice(DeviceConfig cfg);
};
