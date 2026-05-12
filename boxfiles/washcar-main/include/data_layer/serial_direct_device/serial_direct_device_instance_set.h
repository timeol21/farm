#ifndef SERIAL_DIRECT_DEVICE_INSTANCE_SET_H
#define SERIAL_DIRECT_DEVICE_INSTANCE_SET_H

#include <vector>
#include <unordered_map>
#include "data_layer/sensor/temp_humid_sensor.h"
#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/device/device_data.h"
class SerialDirectDeviceInstanceSet {

    public:
        SerialDirectDeviceInstanceSet() = default;
        ~SerialDirectDeviceInstanceSet() = default;

        SerialDirectDeviceInstanceSet(std::vector<TempHumidSensor> sensorList);

        std::vector<TempHumidSensorStatus> getSensorStatusList();

        std::vector<DeviceData> acquisitionTempHumidSensorData();
    
    private:
        std::unordered_map<std::string, TempHumidSensor>  sensors_;
};

#endif