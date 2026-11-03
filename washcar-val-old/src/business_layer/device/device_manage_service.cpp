#include "business_layer/device/device_manage_service.h"
// #include "solenoid_value.h"
#include "common/log/log_manager.h"
#include <vector>
#include <iostream>


DeviceManageService::DeviceManageService(
                                         //PlcInstanceSet&& plcInstances,
                                         FxPlcInstanceSet&& fxPlcSet,
                                         CameraInstanceSet&& cameraInstances
                                        ) 
    :   //plcInstances_(std::move(plcInstances)),
     fxPlcSet_(std::move(fxPlcSet)) ,
    cameraInstances_(std::move(cameraInstances))
       
{

}
//






DeviceManageService::~DeviceManageService() {

}

BoxDeviceStatus DeviceManageService::getDeviceStatus() {
    //const std::vector<SolenoidStatus> solenoidValues = plcInstances_.getPlcSolenoidStatusList();
    //const std::vector<InfraredSensorStatus> infraredSensorStatuses = plcInstances_.getPlcInfraredSensorStatusList();
    //const std::vector<PlcSmokeDetectorStatus> smokeDetectorStatuses = plcInstances_.getPlcSmokeDetectorStatusList();
    //const std::vector<PlcWaterLevelSensorStatus> waterSensores = plcInstances_.getPlcWaterLevelSensorStatusList();
    const std::vector<CameraStatus> cameras = cameraInstances_.getCameraStatusList();
    //std::vector<FxPlcStatus> emptyFxPlcList;
    const std::vector<FxPlcStatus> fxPlcStatusList = fxPlcSet_.getFxPlcStatusList();

    //return BoxDeviceStatus(solenoidValues, cameras, tempHumidSensors,infraredSensorStatuses,smokeDetectorStatuses,waterSensores,doorLocks);
    return BoxDeviceStatus(cameras, fxPlcStatusList);
}


//fx置位m位
bool DeviceManageService::forceM(const std::string& plcId, int mDecimal, bool turnOn) {
    IPlcInstance* plc = fxPlcSet_.getPlc(plcId);
    if (!plc) {
        LOG_ERROR(std::string("[DeviceManage] forceM: PLC ") + plcId + " 未找到, m=" + std::to_string(mDecimal) + ", val=" + (turnOn ? "1" : "0"));
        return false;
    }
    return plc->forceM(mDecimal, turnOn);
}
bool DeviceManageService::forceX(const std::string& plcId, int xOctal, bool turnOn) {
    IPlcInstance* plc = fxPlcSet_.getPlc(plcId);
    if (!plc) {
        LOG_ERROR(std::string("[DeviceManage] forceX: PLC ") + plcId + " 未找到, x=" + std::to_string(xOctal) + ", val=" + (turnOn ? "1" : "0"));
        return false;
    }
    return plc->forceX(xOctal, turnOn);
}
// DeviceOperationResult DeviceManageService::openSolenoidValue(const PlcDeviceInfo& info) {
//     if(plcInstances_.openPlcSolenoid(info)) return DeviceOperationResult(0,"打开成功"); 
//     return DeviceOperationResult(-1,"打开失败");

// DeviceOperationResult DeviceManageService::closeSolenoidValue(const PlcDeviceInfo& info) {
//     if(plcInstances_.closePlcSolenoid(info)) return DeviceOperationResult(0,"关闭成功");
//     return DeviceOperationResult(-1,"关闭失败");


CameraHistoryVideo DeviceManageService::getCameraHistoryVideo( const CameraInfo& info) {
    return cameraInstances_.getCameraHistoryVideo(info.getDeviceId());
}




std::vector<DeviceData> DeviceManageService::deviceDataAcquisition(int deviceType) {
    switch(deviceType) {
        //case 0: return plcInstances_.acquisitionPlcSolenoidData();
        case 2: return cameraInstances_.acquisitionCameraData();
        //case 4: return plcInstances_.acquisitionPlcInfraredSensorData();
        //case 5: return plcInstances_.acquisitionPlcSmokeDetectorData();
        //case 6: return plcInstances_.acquisitionPlcWaterLevelSensorData();
        case 7: return fxPlcSet_.acquisitionFxPlcData();
 
        default: return {};
    }
    
}
    
