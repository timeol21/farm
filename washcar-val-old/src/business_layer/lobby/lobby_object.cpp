#include "business_layer/lobby/lobby_object.h"
#include <iostream>

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
    plcId = j.value("plcId", "fx_plc_01");
    mode = j.value("mode", "");
    point = j.value("point", "");
    reqSource = j.value("reqSource", "http");
    if (j.contains("time") && !j["time"].is_null()) {
        if (j["time"].is_string()) {
            time = j["time"].get<std::string>();
        } else {
            time = j["time"].dump();
        }
    } else {
        time = "";
    }
    cameraId = j.value("cameraId", "camera_001");
    
    if (j.contains("points") && j["points"].is_array()) {
        for (const auto& p : j["points"]) {
            if (p.is_string()) {
                queryPoints[p.get<std::string>()] = 0;
                std::cout << "query:" << p.get<std::string>() << std::endl;
            }
        }
    }
}

std::string WashCarOperation::getBoxNo() const { return boxNo; }
std::string WashCarOperation::getPlcId() const { return plcId; }
std::string WashCarOperation::getMode() const { return mode; }
std::string WashCarOperation::getPoint() const { return point; }
std::string WashCarOperation::getReqSource() const { return reqSource; }
std::string WashCarOperation::getTime() const { return time; }
std::string WashCarOperation::getProductId() const { return productId; }
std::string WashCarOperation::getDeviceNum() const { return deviceNum; }
std::string WashCarOperation::getCameraId() const { return cameraId; }
const std::unordered_map<std::string, int>& WashCarOperation::getQueryPoints() const {
    return queryPoints;
}
bool WashCarOperation::isValid() const {
    //return !boxNo.empty() && !plcId.empty();
    return !plcId.empty();
}
void WashCarOperation::setReqSource(const std::string& reqSource_) {
    reqSource = reqSource_;
}
void WashCarOperation::setBoxNo(const std::string& boxNo_) {
    boxNo = boxNo_;
}
void WashCarOperation::setProductId(const std::string& id) {
    productId = id;
}
void WashCarOperation::setDeviceNum(const std::string& num) {
    deviceNum = num;
}
void WashCarOperation::setCameraId(const std::string& cameraId_) {
    cameraId = cameraId_;
}
void WashCarOperation::addQueryPoint(const std::string& name, int value) {
    queryPoints[name] = value;
}

int WashCarOperation::getPointDecimal() const {
    if (point.empty() || point == "all") {
        return -1;
    }
    if (point.length() >= 2) {
        std::string addrStr = point.substr(1);
        try {
            return std::stoi(addrStr);
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

std::string WashCarOperation::getPointType() const {
    if (point.empty() || point == "all") {
        return point;
    }
    if (point.length() >= 2) {
        return point.substr(0, 1);
    }
    return "";
}
