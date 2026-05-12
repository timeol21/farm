#include "business_layer/lobby/lobby_object.h"

SolenoidValveOperation::SolenoidValveOperation(const json& j){
        deviceId  = j.value("deviceId", "");
        cmd       = j.value("cmd", "");
        plcId     = j.value("plcId", "");
        reqSource = j.value("reqSource", "");
}


const std::string& SolenoidValveOperation::getDeviceId() const{
    return deviceId;
}
const std::string& SolenoidValveOperation::getCmd() const{
    return cmd;
}
const std::string& SolenoidValveOperation::getPlcId() const{
    return plcId;
}

const std::string& SolenoidValveOperation::getReqSource() const{
    return reqSource;
}
bool SolenoidValveOperation::isValid() {
    return !deviceId.empty() && !cmd.empty() && !plcId.empty();
}

WashCarOperation::WashCarOperation(const json& j) {
    boxNo = j.value("boxNo", "");
    plcId = j.value("plcId", "");
    mode = j.value("mode", "auto");
    reqSource = j.value("reqSource", "http");
    time = j.value("time", "");
}

std::string WashCarOperation::getBoxNo() const { return boxNo; }
std::string WashCarOperation::getPlcId() const { return plcId; }
std::string WashCarOperation::getMode() const { return mode; }
std::string WashCarOperation::getReqSource() const { return reqSource; }
std::string WashCarOperation::getTime() const { return time; }
bool WashCarOperation::isValid() const {
    return !boxNo.empty() && !plcId.empty();
}