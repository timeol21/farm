#pragma once

#include "business_layer/device/device_manage_service.h"
#include "business_layer/device/device_status_cache.h"
#include "business_layer/device/device_acquisition_task.h"
#include "business_layer/device/real_time_frame_cache.h"

#include "data_layer/camera/camera_real_time_frame.h"

#include<thread>
#include<atomic>
#include<mutex>

class IDeviceService{
    public:
        virtual ~IDeviceService() = default;

        virtual BoxDeviceStatus viewAllDeviceStatus() = 0;
        // virtual BoxDeviceRealTimeData getBoxDeviceRealTimeData() = 0;

        //电磁阀控制
        //virtual DeviceOperationResult openSolenoidValue( const PlcDeviceInfo& info) = 0;
       // virtual DeviceOperationResult closeSolenoidValue( const PlcDeviceInfo& info) = 0;
        
        //小车控制
        // virtual DeviceOperationResult controlCarRotation( const CarControl& car) = 0;
        
        //摄像头
        virtual CameraRealTimeFrame getCameraRealTimeFrame( const CameraInfo& info) = 0;
        virtual CameraHistoryVideo viewCameraHistoryVideo( const CameraInfo& info) = 0;

        //fx plc
        // virtual bool openFxSolenoid(const std::string& plcId, int yOctal) = 0;
        // virtual bool closeFxSolenoid(const std::string& plcId, int yOctal) = 0;
        //置位m点
        virtual bool forceFxM(const std::string& plcId, int mDecimal, bool turnOn) = 0;
        virtual bool forceFxX(const std::string& plcId, int xOctal, bool turnOn) = 0; 
        //获取状态变化和清除
        virtual std::vector<FxPlcStatus> getChangedFxPlcStatus() = 0;
        virtual void clearChangedFxPlcStatus(const std::vector<std::string>& plcIds) = 0;

        //雷达
        // virtual RadarPointCloud getRadarPointCloudData( const RadarInfo& info) = 0;

        //盒子配置
        // virtual BoxConfigResult configBoxDeviceParams( const BoxDeviceParam& params) = 0;

    

};


class DeviceService : public IDeviceService{
    public: 
        DeviceService(DeviceManageService& deviceManageService,
                      DeviceStatusCache& deviceStatusCache,
                      DeviceAcquisitionTask& deviceAcuqisitionTask,
                      RealTimeFrameCache& realTimeFrameCache);

        ~DeviceService();
        BoxDeviceStatus viewAllDeviceStatus() override;
        // BoxDeviceRealTimeData getBoxDeviceRealTimeData() override;

        //fx plc
        bool forceFxM(const std::string& plcId, int mDecimal, bool turnOn) override;
        bool forceFxX(const std::string& plcId, int xOctal, bool turnOn) override; 
        std::vector<FxPlcStatus> getChangedFxPlcStatus() override;
        void clearChangedFxPlcStatus(const std::vector<std::string>& plcIds) override;
         //电磁阀控制
       // DeviceOperationResult openSolenoidValue( const PlcDeviceInfo& info) override;
        //DeviceOperationResult closeSolenoidValue( const PlcDeviceInfo& info) override;

        //小车控制
        // DeviceOperationResult controlCarRotation( const CarControl& car) override;
        
        //摄像头
        CameraRealTimeFrame getCameraRealTimeFrame( const CameraInfo& info) override;
        CameraHistoryVideo viewCameraHistoryVideo( const CameraInfo& info) override;

        //雷达
        // RadarPointCloud getRadarPointCloudData( const RadarInfo& info) override;

        //盒子配置
        // BoxConfigResult configBoxDeviceParams( const BoxDeviceParam& params) override;

        void startTimer();
        void stopTimer();

    private:
        //数据采集
        void devicesDataCollection(int deviceType);
        //定时器
        void timerLoop();

        DeviceStatusCache& deviceStatusCache_;
        DeviceManageService& deviceManageService_;
        DeviceAcquisitionTask& deviceAcquisitionTask_;
        RealTimeFrameCache& realTimeFrameCache_;
        
        std::thread _timerThread;
        std::atomic<bool> _running {false};
        std::mutex _mutex;
};