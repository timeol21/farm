#pragma once

#include "data_layer/box/box_device_status.h"
#include "data_layer/device/device_operation_result.h"
#include "data_layer/camera/camera_history_video.h"
#include "data_layer/radar/radar_point_cloud.h"
#include "data_layer/box/box_config_result.h"
#include "data_layer/car/car_control.h"
#include "data_layer/camera/camera_info.h"
#include "data_layer/radar/radar_info.h"
#include "data_layer/box/box_device_param.h"

#include "data_layer/box/box_instance.h"
#include "data_layer/camera/camera_instance_set.h"

#include "data_layer/gpio_device/gpio_device_instance_set.h"
#include "data_layer/serial_direct_device/serial_direct_device_instance_set.h"

#include "data_layer/fx_plc/fx_plc_instance_set.h"


class IDeviceManageService {
    public: 

        virtual BoxDeviceStatus getDeviceStatus() = 0;
        // virtual BoxDeviceRealTimeData getDeviceRealTimeData() = 0;

        //virtual DeviceOperationResult openSolenoidValue(const PlcDeviceInfo& info) = 0;
        //virtual DeviceOperationResult closeSolenoidValue(const PlcDeviceInfo& info) = 0;

        // virtual DeviceOperationResult controlCarRotation(const CarControl& car) = 0;

        virtual CameraHistoryVideo getCameraHistoryVideo(const CameraInfo& info) = 0;

        // virtual RadarPointCloud getRadarPointCloudData(const RadarInfo& info) = 0;

        // virtual BoxConfigResult BoxDeviceParamsConfig( const BoxDeviceParam& params) = 0;

        // 设备数据采集，返回DeviceData的列表，deviceType: 0-电磁阀, 1-传感器, 2-摄像头,3-门锁
        virtual std::vector<DeviceData> deviceDataAcquisition(int deviceType) = 0; 

};

class DeviceManageService : public IDeviceManageService { 
    public:
        DeviceManageService() = default ;
        ~DeviceManageService() ;
        DeviceManageService(FxPlcInstanceSet&& fxPlcSet,
                           
                            CameraInstanceSet&& cameraInstances,
                            GPIODeviceInstanceSet&& gpioInstanceSet,
                            SerialDirectDeviceInstanceSet&& serialInstances
                            );
                        //fx是三菱fx3u
         FxPlcInstanceSet& getFxPlcSet() { return fxPlcSet_; }
        const FxPlcInstanceSet& getFxPlcSet() const { return fxPlcSet_; }
         //PlcInstanceSet&& plcInstances,
        
        //m点置位
        bool forceM(const std::string& plcId, int mDecimal, bool turnOn);

        DeviceManageService(DeviceManageService&&) = default;
        DeviceManageService& operator=(DeviceManageService&&) = default;
        DeviceManageService(const DeviceManageService&) = delete;
        DeviceManageService& operator=(const DeviceManageService&) = delete;
        BoxDeviceStatus getDeviceStatus() override;
        //BoxDeviceRealTimeData getDeviceRealTimeData() override;

        //DeviceOperationResult openSolenoidValue(const PlcDeviceInfo& info) override;
        //DeviceOperationResult closeSolenoidValue(const PlcDeviceInfo& info) override;

        DeviceOperationResult lockDoorLock(const GPIODeviceSimpleInfo& info);
        DeviceOperationResult unlockDoorLock(const GPIODeviceSimpleInfo& info);

        // DeviceOperationResult controlCarRotation(const CarControl& car) override;

        CameraHistoryVideo getCameraHistoryVideo(const CameraInfo& info) override;

        // RadarPointCloud getRadarPointCloudData(const RadarInfo& info) override;

        // BoxConfigResult boxDeviceParamsConfig( const BoxDeviceParams& params) override;

        std::vector<DeviceData> deviceDataAcquisition(int deviceType) override;

    private:

        // BoxInstance boxInstance_;

        //PlcInstanceSet plcInstances_;//原modbus设备

        FxPlcInstanceSet fxPlcSet_;

        GPIODeviceInstanceSet gpioInstanceSet_;

        SerialDirectDeviceInstanceSet serialInstances_;

        CameraInstanceSet cameraInstances_;

        // RadarInstanceSet _radarInstances;

        // CarInstanceSet _carInstances;
};
