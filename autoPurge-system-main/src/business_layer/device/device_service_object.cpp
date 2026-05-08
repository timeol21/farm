#include "business_layer/device/device_service_object.h"
#include <chrono>
#include <thread>

bool DeviceResult::isOk() const{
    return status_ == DeviceExecStatus::Success;
}
    
DeviceExecStatus DeviceResult::getStatus(){
    return status_;
}

const std::string& DeviceResult::getMessage() const{
    return message_;
}

uint64_t DeviceResult::getTimestamp() const{
     return timestamp_;
}

DeviceResult DeviceResult::Ok(){
    return DeviceResult(DeviceExecStatus::Success);
}

DeviceResult DeviceResult::Fail(const std::string& msg){
    return DeviceResult(DeviceExecStatus::Failed, msg);
}   

DeviceResult DeviceResult::Timeout(const std::string& msg ){
    return DeviceResult(DeviceExecStatus::Timeout, msg);
}

DeviceResult DeviceResult::Exception(const std::string& msg){
    return DeviceResult(DeviceExecStatus::Exception, msg);
}

uint64_t DeviceResult::now() const{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
}




DeviceCommand DeviceCommand::Start(const std::string& deviceId){
     return DeviceCommand(deviceId, DeviceAction::Start);
}

DeviceCommand DeviceCommand::Stop(const std::string& deviceId) {
    return DeviceCommand(deviceId, DeviceAction::Stop);
} 

DeviceCommand DeviceCommand::Open(const std::string& deviceId){
    return DeviceCommand(deviceId, DeviceAction::Open);
}

DeviceCommand DeviceCommand::Close(const std::string& deviceId){
    return DeviceCommand(deviceId, DeviceAction::Close);
}

DeviceCommand DeviceCommand::SetValue(const std::string& deviceId, int value){
    return DeviceCommand(deviceId, DeviceAction::SetValue, value);
}

const std::string& DeviceCommand::getDeviceId() const{
    return deviceId_;
}

DeviceAction DeviceCommand::getAction() const{
    return action_;
}

int DeviceCommand::getValue() const{
    return value_;
}

const std::string& DeviceCommand::getOperatorName() const{
    return operatorName_;
}

uint64_t DeviceCommand::getTimestamp() const{
     return timestamp_;
}

const std::map<std::string, std::string>& DeviceCommand::getExtraParams() const {
    return extraParams_;
}

void DeviceCommand::addParam(const std::string& key, const std::string& value){
     extraParams_[key] = value;
}

uint64_t DeviceCommand::now(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
}



DeviceHealthStatus::Status DeviceHealthStatus::getStatus() const{
    return status_;
}

const std::string& DeviceHealthStatus::getMessage() const{
    return message_;
}


bool DeviceHealthStatus::isNormal() const { return status_ == Status::Normal; }
bool DeviceHealthStatus::isTimeout() const { return status_ == Status::Timeout; }
bool DeviceHealthStatus::isException() const { return status_ == Status::Exception; }
