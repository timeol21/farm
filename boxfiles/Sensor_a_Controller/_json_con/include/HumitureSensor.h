#ifndef HUMITURE_SENSOR_H
#define HUMITURE_SENSOR_H

#include "SerialDirectDevice.h"

class HumitureSensor : public SerialDirectDevice {
public:
    HumitureSensor(const std::string& deviceId = "humiture_sensor_1");
    ~HumitureSensor() override = default;

    // 读取温湿度数据（差异化逻辑）
    bool readData() override;
};

#endif // HUMITURE_SENSOR_H