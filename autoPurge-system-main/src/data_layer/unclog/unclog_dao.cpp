#include "data_layer/unclog/unclog_dao.h"
#include "common/config/config_load.h"


UnclogDao::UnclogDao(){

}

UnclogDao::~UnclogDao(){

}


bool UnclogDao::getMonitorTime(){

}

RawDeviceInfo UnclogDao::getDeviceInfo(){
    auto& config = SystemConfig::instance();
    RawDeviceInfo data;
    data.plcDevices = config.getServices().device_service.config.getPlcDevices();
    data.sensors = config.getServices().device_service.config.getSensors();
    data.device_config = config.getServices().device_service;
    data.cameras = config.getServices().device_service.config.getCameras();
    return data;
}

bool UnclogDao::getUnclogRecords(){
    return true;
}

