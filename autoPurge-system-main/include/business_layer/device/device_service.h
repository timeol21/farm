#pragma once


#include "business_layer/device/device_service_object.h"
#include "business_layer/device/device_runtime_manager.h"
#include "business_layer/device/device_state_query.h"
#include "common/application/lifecycle.h"


//操作所有设备
class IDeviceService{
public:

   virtual ~IDeviceService() = default;

   virtual DeviceResult execute(const DeviceCommand& cmd) = 0;

   virtual DeviceResult resetDevice(const DeviceCommand& cmd) = 0; //重置设备 （统一：停止/关闭/恢复所有设备）

   virtual DeviceResult checkDeviceAbnormal(const DeviceCommand& cmd) = 0;

   virtual DeviceStatusView getDeviceStatus(const DeviceStatusQuery& query) = 0; //查看设备状态

   virtual CameraKeyFrame getCameraKeyFrame(const DeviceCommand& cmd) = 0; //获取指定摄像头的关键帧

   virtual RadarData getRadarRealTimeData(const DeviceCommand& cmd) = 0; // 获取雷达实时检测数据
   
   
};


class DeviceService : public IDeviceService , public ILifecycle{
public:

   DeviceService(IDeviceRuntimeManager& runtimeManager,
                 IDeviceStateQuery& stateQuery,
                 std::shared_ptr<FrameBuffer> frameBuffer
               )
      :runtimeManager_(runtimeManager),
       stateQuery_(stateQuery)
   {

   }
   ~DeviceService();

   void start() override;

   void stop() override;

   DeviceResult execute(const DeviceCommand& cmd) override;

   DeviceResult resetDevice(const DeviceCommand& cmd)override;

   DeviceResult checkDeviceAbnormal(const DeviceCommand& cmd)override;

   DeviceStatusView getDeviceStatus(const DeviceStatusQuery& query)override;

   CameraKeyFrame getCameraKeyFrame(const DeviceCommand& cmd) override; //获取指定摄像头的关键帧

   RadarData getRadarRealTimeData(const DeviceCommand& cmd) override; // 获取雷达实时检测数据


private:

   DeviceResult buildResultByHealth(const std::string& deviceId, const DeviceHealthStatus& health);

   DeviceResult startHighPressurePump(const DeviceCommand& cmd);

   DeviceResult openSolenoidValve(const DeviceCommand& cmd);   


private:

   IDeviceRuntimeManager& runtimeManager_; //设备运行管理器 
   // IDeviceCommandExecutor& commandExecutor_;
   IDeviceStateQuery& stateQuery_;
};

