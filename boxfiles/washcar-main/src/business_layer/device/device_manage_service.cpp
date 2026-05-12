#include "business_layer/device/device_manage_service.h"
// #include "solenoid_value.h"
#include <vector>
#include <iostream>


DeviceManageService::DeviceManageService(
                                         //PlcInstanceSet&& plcInstances,
                                         FxPlcInstanceSet&& fxPlcSet,
                                         CameraInstanceSet&& cameraInstances,
                                         GPIODeviceInstanceSet&& gpioInstanceSet,
                                         SerialDirectDeviceInstanceSet&& serialInstances                                         
                                        ) 
    :   //plcInstances_(std::move(plcInstances)),
     fxPlcSet_(std::move(fxPlcSet)) ,
    cameraInstances_(std::move(cameraInstances)),
        gpioInstanceSet_(std::move(gpioInstanceSet)),
        serialInstances_(std::move(serialInstances))
       
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
    const std::vector<TempHumidSensorStatus> tempHumidSensors = serialInstances_.getSensorStatusList();
    const std::vector<DoorLockStatus> doorLocks = gpioInstanceSet_.getDoorLockStatusList();
    // const std::vector<RadarStatus> radars = _radarInstanceSet.getRadars();
    // const std::vector<CarStatus> cars = _carInstanceSet.getCars();
    std::vector<FxPlcStatus> emptyFxPlcList;

    //return BoxDeviceStatus(solenoidValues, cameras, tempHumidSensors,infraredSensorStatuses,smokeDetectorStatuses,waterSensores,doorLocks);
    return BoxDeviceStatus(cameras, tempHumidSensors, doorLocks,emptyFxPlcList);
}


//fx置位m位
bool DeviceManageService::forceM(const std::string& plcId, int mDecimal, bool turnOn) {
    FxPlcInstance* plc = fxPlcSet_.getPlc(plcId);
    if (!plc) {
        std::cerr << "[DeviceManage] forceM: PLC " << plcId << " 未找到" << std::endl;
        return false;
    }
    return plc->forceM(mDecimal, turnOn);
}
// DeviceOperationResult DeviceManageService::openSolenoidValue(const PlcDeviceInfo& info) {
//     if(plcInstances_.openPlcSolenoid(info)) return DeviceOperationResult(0,"打开成功"); 
//     return DeviceOperationResult(-1,"打开失败");
// }

// DeviceOperationResult DeviceManageService::closeSolenoidValue(const PlcDeviceInfo& info) {
//     if(plcInstances_.closePlcSolenoid(info)) return DeviceOperationResult(0,"关闭成功");
//     return DeviceOperationResult(-1,"关闭失败");
// }

DeviceOperationResult DeviceManageService::lockDoorLock(const GPIODeviceSimpleInfo& info) {
    if(gpioInstanceSet_.lockDoorLock(info)) return DeviceOperationResult(0,"上锁成功");
    return DeviceOperationResult(-1,"上锁失败");
}
DeviceOperationResult DeviceManageService::unlockDoorLock(const GPIODeviceSimpleInfo& info) {
    if(gpioInstanceSet_.unlockDoorLock(info)) return DeviceOperationResult(0,"解锁成功");
    return DeviceOperationResult(-1,"解锁失败");
}

// DeviceOperationResult DeviceManageService::controlCarRotation( const CarControl& car) {
//     int result = _carInstanceSet.controlCarRotation(car);
//     return new DeviceOperationResult(result, "控制成功" );
// }

CameraHistoryVideo DeviceManageService::getCameraHistoryVideo( const CameraInfo& info) {
    return cameraInstances_.getCameraHistoryVideo(info.getDeviceId());
}

// RadarPointCloud DeviceManageService::getRadarPointCloudData( const RadarInfo& info) {
//     return radarInstanceSet_.getRadarPointCloudData(info.getDeviceId());
// }

// BoxConfigResult DeviceManageService::boxDeviceParamsConfig( const BoxDeviceParam& params) {
//     return boxInstance_.configBoxDeviceParams(params);
// }


std::vector<DeviceData> DeviceManageService::deviceDataAcquisition(int deviceType) {
    switch(deviceType) {
        //case 0: return plcInstances_.acquisitionPlcSolenoidData();
        case 1: return serialInstances_.acquisitionTempHumidSensorData();
        case 2: return cameraInstances_.acquisitionCameraData();
        case 3: return gpioInstanceSet_.acquisitionDoorLockData();
        //case 4: return plcInstances_.acquisitionPlcInfraredSensorData();
        //case 5: return plcInstances_.acquisitionPlcSmokeDetectorData();
        //case 6: return plcInstances_.acquisitionPlcWaterLevelSensorData();
        //case 7: return fxplcInstanceSet_.acquisitionFxPlcData();
 
        default: return {};
    }
    
}
    