#ifndef INFRARED_SENSOR_H
#define INFRARED_SENSOR_H

#include "PLCComponentDevice.h"

class InfraredSensor : public PLCComponentDevice {
public:
    InfraredSensor(const std::string& deviceId = "infrared_sensor_1");
    ~InfraredSensor() override = default;

    // 读取红外数据（差异化逻辑）
    bool readData() override;
};

#endif // INFRARED_SENSOR_H