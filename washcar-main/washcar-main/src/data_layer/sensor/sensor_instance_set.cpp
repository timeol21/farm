#include "data_layer/sensor/sensor_instance_set.h"

SensorInstanceSet::SensorInstanceSet() {

}

SensorInstanceSet::~SensorInstanceSet() {

}

const std::vector<TempHumidSensorStatus> SensorInstanceSet::getSensorStatusList() const {
    std::vector<TempHumidSensorStatus> sensorStatusList;
    sensorStatusList.reserve(sensors_.size());
    for(const auto&[key, sensor] : sensors_) {
        sensorStatusList.push_back(sensor->readSensorData());
     }
    return sensorStatusList;
}


// const std::vector<SensorRealTimeData>& SensorInstanceSet::getSensorRealTimeDataList() const {
//    std::vector<SensorRealTimeData> sensorRealTimeDataList;
//     sensorRealTimeDataList.reserve(sensors_.size());
//     for(const auto[key, sensor] : sensors_) {
//         sensorRealTimeDataList.push_back(sensor->getSensorRealTimeData());
//      }
//     return sensorRealTimeDataList;
// }

std::vector<std::unique_ptr<DeviceData> > SensorInstanceSet::acquisitionSensorData() {
    std::vector<std::unique_ptr<DeviceData> > sensorDataList;
    for(const auto&[key, sensor] : sensors_) {
        sensorDataList.push_back(std::make_unique<DeviceData>(1, sensor->readSensorData()));
    }
    return sensorDataList;
}
