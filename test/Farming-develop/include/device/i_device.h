#pragma once

#include "config/config.h"

class IDevice {
public:
	virtual ~IDevice() = default;
	virtual bool init() = 0;
	virtual bool update() = 0;
	virtual void stop() = 0;

	virtual const DeviceState& getState() const = 0;
	virtual std::string getDeviceId() const = 0;
};