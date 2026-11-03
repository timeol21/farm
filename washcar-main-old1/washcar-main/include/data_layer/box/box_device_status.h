#ifndef BOX_DEVICE_STATUS_H
#define BOX_DEVICE_STATUS_H

//#include "data_layer/plc_device/solenoid_status.h"
#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/camera/camera_status.h"
#include "data_layer/gpio_device/door_lock_status.h"
//#include "data_layer/plc_device/infrared_sensor_status.h"
//#include "data_layer/plc_device/plc_smoke_detector_status.h"
//#include "data_layer/plc_device/plc_water_level_sensor_status.h"
#include "data_layer/gpio_device/door_lock_status.h"
#include "data_layer/fx_plc/fx_plc_status.h"
#include<vector>

class BoxDeviceStatus {
    public:
        BoxDeviceStatus(//const std::vector<SolenoidStatus> solenoidStatuses,
                        const std::vector<CameraStatus> cameraStatuses,
                        const std::vector<TempHumidSensorStatus> sensorStatuses,
                        //const std::vector<InfraredSensorStatus> infraredSensorStatuses,
                        //const std::vector<PlcSmokeDetectorStatus> smokeDetectorStatuses,
                        //const std::vector<PlcWaterLevelSensorStatus> waterSensorStatuses,
                        const std::vector<DoorLockStatus> doorLockStatuses,
                        const std::vector<FxPlcStatus>& fxPlcStatusList
                    );
        BoxDeviceStatus() = default;
        ~BoxDeviceStatus() = default;

        //std::vector<SolenoidStatus> getSolenoidStatusList() const ;
        std::vector<TempHumidSensorStatus> getSensorStatusList() const;
        std::vector<CameraStatus> getCameraStatusList() const;
        //std::vector<InfraredSensorStatus> getInfraredSensorStatusList() const;
        //std::vector<PlcSmokeDetectorStatus> getSmokeDetectorStatusList() const;
        //std::vector<PlcWaterLevelSensorStatus> getWaterLevelSensorStatusList() const;
        std::vector<DoorLockStatus> getDoorLockStatusList() const;
        const std::vector<FxPlcStatus>& getFxPlcStatusList() const;
            
    private:
        //std::vector<SolenoidStatus> solenoidStatuses_;
        std::vector<TempHumidSensorStatus> sensorStatuses_;
        std::vector<CameraStatus> cameraStatuses_;
        //std::vector<InfraredSensorStatus> infraredSensorStatuses_;
        //std::vector<PlcSmokeDetectorStatus> smokeDetectorStatuses_;
        //std::vector<PlcWaterLevelSensorStatus> waterSensorStatuses_;
        std::vector<DoorLockStatus> doorLockStatuses_;
        std::vector<FxPlcStatus> fxPlcStatusList_; 
};

#endif