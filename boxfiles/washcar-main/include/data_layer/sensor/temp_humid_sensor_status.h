#ifndef TEMP_HUMID_SENSOR_STATUS_H
#define TEMP_HUMID_SENSOR_STATUS_H

#include <string>
#include "data_layer/sensor/sensor_types.h"
#include "data_layer/device/device_status.h"

class TempHumidSensorStatus : public DeviceStatus{
    public:
        TempHumidSensorStatus() = default;
        TempHumidSensorStatus(const std::string& deviceId,
                     const int type,
                     const std::string& name,
                     const TempHumidStatus& status,
                     float humidity,
                     float tempature);
        ~TempHumidSensorStatus() override = default;
    
    private:

        TempHumidStatus status_;
        float humidity_;
        float tempature_;


};

#endif