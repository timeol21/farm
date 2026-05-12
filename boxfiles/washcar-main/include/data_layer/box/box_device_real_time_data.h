#ifndef BOX_DEVICE_REAL_TIME_DATA_H
#define BOX_DEVICE_REAL_TIME_DATA_H

#include <vector>
#include "data_layer/box/box_device_real_time_data.h"
#include "data_layer/sensor/sensor_real_time_data.h"
class BoxDeviceRealTimeData {
    public:
        BoxDeviceRealTimeData(//const std::vector<SolenoidRealTimeData>& solenoidRealTimeDataList,
                              const std::vector<SensorRealTimeData>& sensorRealTimeDataList);
        BoxDeviceRealTimeData() = default;
        ~BoxDeviceRealTimeData() = default;

        //std::vector<SolenoidRealTimeData> getSolenoidRealTimeDataList() const;
        std::vector<SensorRealTimeData> getSensorRealTimeDataList() const;

    private:
        //std::vector<SolenoidRealTimeData> solenoidRealTimeDataList_;
        std::vector<SensorRealTimeData> sensorRealTimeDataList_;
};


#endif