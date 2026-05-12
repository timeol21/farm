#include "data_layer/sensor/temp_humid_sensor_status.h"


TempHumidSensorStatus::TempHumidSensorStatus(const std::string& deviceId,
                           const int type,
                           const std::string& name,
                           const TempHumidStatus& status,
                           float humidity,
                           float tempature) 
    : DeviceStatus(deviceId,type,name),
      status_(status),
      humidity_(humidity),
      tempature_(tempature) {

}