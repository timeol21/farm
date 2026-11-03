#pragma once

#include "device/i_device.h"
#include "utils/json.hpp"

class IActuator : public IDevice {
public:
	virtual bool execute(const nlohmann::json& params) = 0;
};