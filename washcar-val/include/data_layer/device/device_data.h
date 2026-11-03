#ifndef DEVICE_DATA_H
#define DEVICE_DATA_H

// #include "data_layer/plc_device/solenoid_status.h"
// #include "data_layer/plc_device/infrared_sensor_status.h"
// #include "data_layer/plc_device/plc_smoke_detector_status.h"
// #include "data_layer/plc_device/plc_water_level_sensor_status.h"
#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/gpio_device/door_lock_status.h"
#include "data_layer/camera/camera_status.h"
#include "data_layer/fx_plc/fx_plc_status.h"

class DeviceData {
    public:
        DeviceData() = default;
        //DeviceData(int type,const SolenoidStatus solenoidStatus);
        DeviceData(int type,const TempHumidSensorStatus sensorStatus);
        DeviceData(int type,const CameraStatus cameraStatus);
        DeviceData(int type,const DoorLockStatus doorLockStatus);
        // DeviceData(int type,const InfraredSensorStatus infraredSensorStatus);
        // DeviceData(int type,const PlcSmokeDetectorStatus smokeDetectorStatus);
        // DeviceData(int type,const PlcWaterLevelSensorStatus waterLevelSensorStatus);
        DeviceData(int type, const FxPlcStatus& fxPlcStatus);
        int getType() const ;

        //SolenoidStatus getSolenoidStatus() const ;
        TempHumidSensorStatus getSensorStatus() const ;
        CameraStatus getCameraStatus() const ;
        DoorLockStatus getDoorLockStatus() const;
        // InfraredSensorStatus getInfraredSensorStatus() const;
        // PlcSmokeDetectorStatus getPlcSmokeDetectorStatus() const;
        // PlcWaterLevelSensorStatus getPlcWaterLevelSensorStatus() const;
        FxPlcStatus getFxPlcStatus() const;
    
    private:
        int type; // 0: Solenoid, 1: Sensor, 2: Camera, 3: DoorLock, 4: InfraredSensor, 5: SmokeDetector, 6: WaterLevelSensor

       // SolenoidStatus solenoidStatus_ ;
        TempHumidSensorStatus sensorStatus_ ;
        CameraStatus cameraStatus_ ;
        DoorLockStatus doorLockStatus_ ;
        // InfraredSensorStatus infraredSensorStatus_ ;
        // PlcSmokeDetectorStatus smokeDetectorStatus_ ;
        // PlcWaterLevelSensorStatus waterLevelSensorStatus_ ;
        FxPlcStatus fxPlcStatus_;
};

#endif