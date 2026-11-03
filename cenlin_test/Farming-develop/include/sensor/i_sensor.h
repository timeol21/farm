#pragma once

#include "device/i_device.h"

class ISensor : public IDevice {
public:
	virtual ~ISensor() = default;
	virtual bool read() = 0;
};