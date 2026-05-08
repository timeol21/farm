#pragma once
#include "data_layer/device/device_data_object.h"
#include <vector>

struct ModbusParser {
    static SensorData parseTempHum(const std::vector<uint8_t>& resp);
}; 