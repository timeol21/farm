#include "business_layer/washcar/washcar_operation.h"

#include <iostream>

WashCarOperation::WashCarOperation(const json& j) {
    // 繁易报文里这些字段可能缺席、也可能是 null，统一按"缺席"处理：
    // j.value() 遇到 null 会抛 type_error，直接把整条命令丢掉。
    const auto getStr = [&j](const char* key, const std::string& fallback) {
        if (!j.contains(key) || j[key].is_null() || !j[key].is_string()) {
            return fallback;
        }
        return j[key].get<std::string>();
    };

    boxNo     = getStr("boxNo", "");
    // plcId 不在这里兜底：海天是 fx_plc_01、驿公里是配置里的 tcp plcId，
    // 由业务层按机型补，否则驿公里会拿着 fx_plc_01 去设备层找不到 PLC。
    plcId     = getStr("plcId", "");
    mode      = getStr("mode", "");
    point     = getStr("point", "");
    reqSource = getStr("reqSource", "mqtt");

    if (j.contains("time") && !j["time"].is_null()) {
        if (j["time"].is_string()) {
            time = j["time"].get<std::string>();
        } else {
            time = j["time"].dump();
        }
    }

    if (j.contains("points") && j["points"].is_array()) {
        for (const auto& p : j["points"]) {
            if (p.is_string()) {
                queryPoints[p.get<std::string>()] = 0;
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

const std::unordered_map<std::string, int>& WashCarOperation::getQueryPoints() const {
    return queryPoints;
}

bool WashCarOperation::isValid() const {
    return !plcId.empty();
}

void WashCarOperation::setReqSource(const std::string& reqSource_) { reqSource = reqSource_; }
void WashCarOperation::setBoxNo(const std::string& boxNo_) { boxNo = boxNo_; }
void WashCarOperation::setPlcId(const std::string& plcId_) { plcId = plcId_; }
void WashCarOperation::setProductId(const std::string& id) { productId = id; }
void WashCarOperation::setDeviceNum(const std::string& num) { deviceNum = num; }
void WashCarOperation::addQueryPoint(const std::string& name, int value) { queryPoints[name] = value; }

int WashCarOperation::getPointDecimal() const {
    if (point.empty() || point == "all") {
        return -1;
    }
    if (point.length() >= 2) {
        try {
            return std::stoi(point.substr(1));
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
