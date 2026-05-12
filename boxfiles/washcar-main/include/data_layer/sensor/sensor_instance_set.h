#ifndef SENSOR_INSTANCE_SET_H
#define SENSOR_INSTANCE_SET_H

#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/sensor/temp_humid_sensor.h"
#include "data_layer/device/device_data.h"

#include <vector>
#include <unordered_map>
#include <memory>
class SensorInstanceSet {
    public:
        SensorInstanceSet();
        ~SensorInstanceSet();

        const std::vector<TempHumidSensorStatus> getSensorStatusList() const;
         //循环获取传感器实时数据
        // const std::vector<SensorRealTimeData>& getSensorRealTimeDataList() const;

        std::vector<std::unique_ptr<DeviceData> > acquisitionSensorData();

    private:
        std::unordered_map<std::string,std::unique_ptr<TempHumidSensor> > sensors_;
};

#endif