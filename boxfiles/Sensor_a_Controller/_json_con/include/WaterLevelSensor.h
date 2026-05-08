#ifndef WATER_LEVEL_SENSOR_H
#define WATER_LEVEL_SENSOR_H

#include "PLCComponentDevice.h"

class WaterLevelSensor : public PLCComponentDevice {
public:
    WaterLevelSensor(const std::string& deviceId = "water_level_sensor_1");
    ~WaterLevelSensor() override = default;

    // 读取水位数据（差异化逻辑）
    bool readData() override;
};

#endif // WATER_LEVEL_SENSOR_H