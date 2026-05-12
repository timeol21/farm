#pragma once

#include "data_layer/box/box_device_status.h"
//#include "data_layer/plc_device/solenoid_status.h"
#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/camera/camera_status.h"
//#include "data_layer/plc_device/infrared_sensor_status.h"
//#include "data_layer/plc_device/plc_water_level_sensor_status.h"
//#include "data_layer/plc_device/plc_smoke_detector_status.h"
#include "data_layer/gpio_device/door_lock_status.h"
///#include "data_layer/plc_device/plc_device_info.h"
#include "data_layer/gpio_device/gpio_device_simple_info.h"
#include "data_layer/device/device_data.h"

#include "data_layer/fx_plc/fx_plc_status.h" 
#include "business_layer/device/device_change_listener.h" 
#include <list>
#include<unordered_map>
#include<memory>
#include<vector>

class DeviceStatusCache {

    public:

        DeviceStatusCache();
        ~DeviceStatusCache();
        //更新整个盒子状态
        void updateBoxDeviceStatus( const BoxDeviceStatus& devices);
        //根据采集数据更新特定类型设备
        void updateDeviceStatus( const std::vector<DeviceData>& deviceDataList );
        //获取所有设备状态
        BoxDeviceStatus getBoxDeviceStatus();
        bool isBoxDeviceStatusEmpty() const;
        bool isBoxDeviceStatusEmpty();

        //bool isSolenoidOpen( const PlcDeviceInfo& info);
        //bool isSolenoidClose( const PlcDeviceInfo& info);
        bool isDoorLockLock(const GPIODeviceSimpleInfo& info);
        bool getFxPlcStatus(const std::string& plcId, FxPlcStatus& outStatus) const;
        //变化检测监听
        void addListener(std::shared_ptr<IDeviceChangeListener> listener);
        void removeListener(std::shared_ptr<IDeviceChangeListener> listener);

        //多plc扩展
        //  bool getFxPlcStatus(const std::string& plcId, FxPlcStatus& outStatus) const;
    private:
        //void updateSolenoidStatus( const SolenoidStatus& status);
        void updateTempHumidSensorStatus( const TempHumidSensorStatus& status);
        void updateCameraStatus( const CameraStatus& status);
        //void updateInfraredSensorStatus(const InfraredSensorStatus& status);
        //void updatePlcWaterLevelSensorStatus(const PlcWaterLevelSensorStatus& status);
        //void updatePlcSmokeDetectorStatus(const PlcSmokeDetectorStatus& status);
        void updateDoorLockStatus(const DoorLockStatus& status);
        void updateFxPlcStatus(const FxPlcStatus& status);
        //监听广播
        void notifyListeners(const ChangeRecord& change);
        //std::unordered_map<std::string, SolenoidStatus> solenoidStatusMap_;
        std::unordered_map<std::string, TempHumidSensorStatus> sensorStatusMap_;
        std::unordered_map<std::string, CameraStatus> cameraStatusMap_;
        //std::unordered_map<std::string, InfraredSensorStatus> infraredSensorStatusMap_;
        //std::unordered_map<std::string, PlcWaterLevelSensorStatus> waterLevelSensorStatusMap_;
        //std::unordered_map<std::string, PlcSmokeDetectorStatus> smokeDetectorStatusMap_;
        std::unordered_map<std::string, DoorLockStatus> doorLockStatusMap_;
        // std::unordered_map<std::string, std::unique_ptr<RadarStatus> > _radarStatusMap;
        // std::unordered_map<std::string, std::unique_ptr<CarStatus> > _carStatusMap;
        std::unordered_map<std::string, FxPlcStatus> fxPlcStatusMap_;
        std::list<std::weak_ptr<IDeviceChangeListener>> listeners_;
};

