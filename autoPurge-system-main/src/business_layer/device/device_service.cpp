#include "business_layer/device/device_service.h"
#include "common/log/log_manager.h"

DeviceService::~DeviceService(){

}

void DeviceService::start() {
     if(!runtimeManager_.initializeAll()){
          return ;
     }
     if(!runtimeManager_.startAll()){
          return ;
     }
}

void DeviceService::stop() {
     runtimeManager_.shutdownAll();
}

DeviceResult DeviceService::execute(const DeviceCommand& cmd){
     
     bool startOk = runtimeManager_.startDevice(cmd.getDeviceId());
     if (startOk) {
          //正常， 需要构建DeviceResult返回
          return DeviceResult::Ok();
          //记录操作信息
     }
     //不正常：
     auto health = runtimeManager_.getDeviceHealth(cmd.getDeviceId());
     return buildResultByHealth(cmd.getDeviceId(), health);
     //记录操作信息
 
}


DeviceResult DeviceService::resetDevice(const DeviceCommand& cmd){
     runtimeManager_.shutdownDevice(cmd.getDeviceId());
     auto health = runtimeManager_.getDeviceHealth(cmd.getDeviceId());
     return buildResultByHealth(cmd.getDeviceId(), health);
}

DeviceStatusView DeviceService::getDeviceStatus(const DeviceStatusQuery& query){

     
     return DeviceStatusView();
}

DeviceResult DeviceService::checkDeviceAbnormal(const DeviceCommand& cmd) {
     return DeviceResult();
}

CameraKeyFrame DeviceService::getCameraKeyFrame(const DeviceCommand& cmd) {
     return CameraKeyFrame();
} //获取指定摄像头的关键帧

RadarData DeviceService::getRadarRealTimeData(const DeviceCommand& cmd) {
     return RadarData();
} // 获取雷达实时检测数据


DeviceResult DeviceService::buildResultByHealth(const std::string& deviceId, const DeviceHealthStatus& health){
     switch (health.getStatus()) {
        case DeviceHealthStatus::Status::Normal:
            return DeviceResult::Fail("设备[" + deviceId + "]状态正常，但启动失败");

        case DeviceHealthStatus::Status::Exception:
            return DeviceResult::Exception("设备[" + deviceId + "]异常：" + health.getMessage());

        case DeviceHealthStatus::Status::Timeout:
            return DeviceResult::Timeout("设备[" + deviceId + "]启动超时：" + health.getMessage());

        default:
            return DeviceResult::Fail("设备[" + deviceId + "]未知错误");
    }
}

DeviceResult DeviceService::startHighPressurePump(const DeviceCommand& cmd) {

    return DeviceResult();
}

DeviceResult DeviceService::openSolenoidValve(const DeviceCommand& cmd){
     return DeviceResult();
}

