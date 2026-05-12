#include "data_layer/serial_direct_device/serial_direct_device_instance_set.h"

SerialDirectDeviceInstanceSet::SerialDirectDeviceInstanceSet(std::vector<TempHumidSensor> sensorList) {
    for(auto& sensor : sensorList) {
        sensors_.emplace(sensor.getDeviceId(),std::move(sensor));
    }
}

std::vector<TempHumidSensorStatus> SerialDirectDeviceInstanceSet::getSensorStatusList() {
    std::vector<TempHumidSensorStatus> statusList;
    statusList.reserve(sensors_.size());

    for(auto& [key, sensor] : sensors_) {
        statusList.push_back(sensor.readSensorData());
    }

    return statusList;
}

std::vector<DeviceData> SerialDirectDeviceInstanceSet::acquisitionTempHumidSensorData() {
    std::vector<DeviceData> sensorDataList;
    sensorDataList.reserve(sensors_.size());

    for(auto& [key, sensor] : sensors_) {
        TempHumidSensorStatus status = sensor.readSensorData();
        sensorDataList.push_back(DeviceData (1,status) );
    }
    return sensorDataList;
}