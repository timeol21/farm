#include "presentation_layer/http_command.h"
#include "common/log/log_manager.h"
#include "common/topics.h"
#include "common/config/config_manager.h"
#include <sstream>
#include <ctime>

// localtime_r:localtime 用 static buffer,handleClient 是 detached thread,会并发覆盖
static std::string getCurrentTimeStr() {
    time_t now = time(nullptr);
    tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900,
             tm_buf.tm_mon + 1,
             tm_buf.tm_mday,
             tm_buf.tm_hour,
             tm_buf.tm_min,
             tm_buf.tm_sec);
    return std::string(buf);
}

HTTPCommandController::HTTPCommandController(ILobbyService& lobby) : lobbyService(lobby) {
    // 构造函数，初始化lobbyService引用
}

static void parseHttpPath(const std::string& path, std::string& productId, std::string& deviceNum, std::string& command) {
    std::stringstream ss(path);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            parts.push_back(segment);
        }
    }
    if (parts.size() >= 3) {
        productId = parts[0];
        deviceNum = parts[1];
        command = parts[2];
    }

    if (parts.size() == 4) {
        command = command + '/' + parts[3];
    }
        
}

HttpResponse HTTPCommandController::handleHttp(const std::string& path, const std::string& payload){
    std::string productId, deviceNum, command;
    parseHttpPath(path, productId, deviceNum, command);
    LOG_INFO(std::string("[HTTP] request path=") + path
             + ", productId=" + productId
             + ", deviceNum=" + deviceNum
             + ", command=" + command
             + ", payload=" + payload);

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload);
    } catch (...) {
        LOG_ERROR(std::string("[HTTP] invalid JSON, path=") + path + ", payload=" + payload);
        return {400, R"({"error":"Invalid JSON"})"};
    }
    
    if (command == "washcar/start") {
        return handleWashCarStart(j, productId, deviceNum);
    } else if (command == "washcar/stop") {
        return handleWashCarStop(j, productId, deviceNum);
    } else if (command == "washcar/reset") {
        return handleWashCarReset(j, productId, deviceNum);
    } else if (command == "washcar/status") {
        return handleWashCarStatus(j, productId, deviceNum);
    } else if (command == "washcar/readData") {
        return handleWashCarReadData(j, productId, deviceNum);
    } else if (command == "washcar/changeMode") {
        return handleWashCarChangeMode(j, productId, deviceNum);
    } else if (command == "washcar/clearAlarm") {
        return handleWashCarClearAlarm(j, productId, deviceNum);
    } else if (command == "washcar/point") {
        return handleWashCarPoint(j, productId, deviceNum);
    } else if (command == "washcar/query") {
        return handleWashCarQuery(j, productId, deviceNum);
    } else if (command == "washcar/abort") {
        // 驿公里独有: m206 中止洗车
        return handleWashCarAbort(j, productId, deviceNum);
    } else if (command == "washcar/stopWork") {
        // 驿公里独有: m501 停止营业
        return handleWashCarStopWork(j, productId, deviceNum);
    } else if (command == "washcar/stopService") {
        // 驿公里独有: m510 停止营业 2
        return handleWashCarStopService(j, productId, deviceNum);
    } else if (command == "washcar/resetSafe") {
        // 驿公里独有: m203 安全复位
        return handleWashCarResetSafe(j, productId, deviceNum);
    } else if (command == "point/on") {
        return handlePointOnHttp(j, productId, deviceNum);
    } else if (command == "point/off") {
        return handlePointOffHttp(j, productId, deviceNum);
    } else if (command == "point/reset") {
        return handlePointResetHttp(j, productId, deviceNum);
    } else if (command == "point/status") {
        return handlePointStatusHttp(j, productId, deviceNum);
    } else if (command == "alarm/clear") {
        return handleClearAlarmHttp(j, productId, deviceNum);
    }

    LOG_WARNING(std::string("[HTTP] unknown command, path=") + path);
    return {404, R"({"error":"Not found"})"};
}

void HTTPCommandController::handleMqtt(const std::string& topic, const std::string& payload){
    // MQTT commands are handled by MQTTCommandController
}

// ===== 驿公里独有命令 =====

HttpResponse HTTPCommandController::handleWashCarAbort(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarAbort (yiGongLi m206), productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        if (!operation.isValid()) return {400, R"({"error":"Invalid parameters"})"};

        auto result = lobbyService.abortWashCar(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        json resp;
        resp["message"] = "success";
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP abortWashCar exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarStopWork(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarStopWork (yiGongLi m501), productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        if (!operation.isValid()) return {400, R"({"error":"Invalid parameters"})"};

        auto result = lobbyService.stopWork(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        json resp;
        resp["message"] = "success";
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP stopWork exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarStopService(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarStopService (yiGongLi m510), productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        if (!operation.isValid()) return {400, R"({"error":"Invalid parameters"})"};

        auto result = lobbyService.stopService(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        json resp;
        resp["message"] = "success";
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP stopService exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarResetSafe(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarResetSafe (yiGongLi m203), productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        if (!operation.isValid()) return {400, R"({"error":"Invalid parameters"})"};

        auto result = lobbyService.resetSafe(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        json resp;
        resp["message"] = "success";
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP resetSafe exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarStart(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarStart, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.startWashCar(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();
        if (data.getCode() == ErrorCode::Code::WASHCAR_JOG_EXECUTING) {
            return {403, R"({"message":"jog executing"})"};
        }

        //const auto& status = data.getStatus();
        //int d142 = 0;
        //if (status.count("d142")) {
        //    d142 = status.at("d142");
        //}

        json resp;
        //resp["d142"] = d142;
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar start exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarStop(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarStop, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.stopWashCar(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();

        json resp;
        resp["m0"] = 1;
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar stop exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarReset(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarReset, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.resetWashCar(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();
        if (data.getCode() == ErrorCode::Code::WASHCAR_JOG_EXECUTING) {
            return {403, R"({"message":"jog executing"})"};
        }

        std::string pointName = "all";
        if (j.contains("point")) {
            pointName = j["point"].get<std::string>();
        }

        json resp;
        resp[pointName] = 0;
        resp["plcId"] = washcar::cfg::ConfigManager::instance().plcTcp().plcId;
        resp["boxNo"] = washcar::cfg::ConfigManager::instance().box().boxNo;
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar reset exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarStatus(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarStatus, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.getWashCarStatus(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();
        nlohmann::json resp;
        resp["code"] = data.getCode();
        resp["message"] = data.getMessage();
        resp["step"] = data.getCurrentStep();
        return { 200, resp.dump() };
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar status exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarReadData(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarReadData, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }

        auto result = lobbyService.getWashCarStatus(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        

        auto& data = result.data.value();
        const std::vector<FxPlcStatus> fxList = lobbyService.viewPlcStatus();
        if (fxList.empty()) {
            return {400, R"({"error":"no plc"})"};
        }
        
        auto fx = fxList.front();
        uint16_t d142 = 0;
        bool y74 = false, y51 = false, y50 = false, y52 = false;
        bool m1 = false, m187 = false;
        
        fx.getDRegister(142, d142);
        fx.getYBit(74, y74);
        fx.getYBit(51, y51);
        fx.getYBit(50, y50);
        fx.getYBit(52, y52);
        fx.getMBit(1, m1);
        fx.getMBit(187, m187);
        
        std::string response = "{\"d142\":" + std::to_string(d142) +
            ",\"y74\":" + (y74 ? "1" : "0") +
            ",\"y51\":" + (y51 ? "1" : "0") +
            ",\"y50\":" + (y50 ? "1" : "0") +
            ",\"y52\":" + (y52 ? "1" : "0") +
            ",\"m1\":" + (m1 ? "1" : "0") +
            ",\"m187\":" + (m187 ? "1" : "0") +
            ",\"code\":" + std::to_string(static_cast<int>(data.getCode())) +
            ",\"message\":\"" + data.getMessage() + "\"}";
        
        return {200, response};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar readData exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarChangeMode(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarChangeMode, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.changeMode(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        return {200, "{\"code\":0,\"message\":\"success\"}"};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar changeMode exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarClearAlarm(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarClearAlarm, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.clearAlarmHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();
        return {200, "{\"code\":" + std::to_string(static_cast<int>(data.getCode())) + ",\"message\":\"" + data.getMessage() + "\"}"};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar clearAlarm exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handlePointOnHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handlePointOnHttp, productId=") + productId + ", deviceNum=" + deviceNum + ", point=" + (j.contains("point") ? j["point"].dump() : "null"));
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.pointOnHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        auto& data = result.data.value();
        std::string pointName = j.contains("point") ? j["point"].get<std::string>() : "unknown";

        json resp;
        resp[pointName] = 1;
        resp["plcId"] = "fx_plc_01";
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP point on exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handlePointOffHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handlePointOffHttp, productId=") + productId + ", deviceNum=" + deviceNum + ", point=" + (j.contains("point") ? j["point"].dump() : "null"));
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }

        auto result = lobbyService.pointOffHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        auto& data = result.data.value();
        std::string pointName = j.contains("point") ? j["point"].get<std::string>() : "unknown";

        json resp;
        resp[pointName] = 0;
        resp["plcId"] = "fx_plc_01";
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP point off exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handlePointResetHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handlePointResetHttp, productId=") + productId + ", deviceNum=" + deviceNum + ", point=" + (j.contains("point") ? j["point"].dump() : "null"));
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.pointResetHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        auto& data = result.data.value();
        std::string pointName = j.contains("point") ? j["point"].get<std::string>() : "all";

        json resp;
        resp[pointName] = 0;
        resp["plcId"] = "fx_plc_01";
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP point reset exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handlePointStatusHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handlePointStatusHttp, productId=") + productId + ", deviceNum=" + deviceNum + ", point=" + (j.contains("point") ? j["point"].dump() : "null"));
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.pointStatusHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }
        
        auto& data = result.data.value();
        const auto& status = data.getStatus();
        std::string pointName = j.contains("point") ? j["point"].get<std::string>() : "all";

        json resp;
        if (pointName == "all") {
            resp["point"] = status;
        } else {
            int pointValue = 0;
            if (status.count(pointName)) {
                pointValue = status.at(pointName);
            }
            resp[pointName] = pointValue;
        }
        resp["plcId"] = "fx_plc_01";
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", static_cast<int>(data.getCode())}, {"msg", data.getMessage()}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP point status exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleClearAlarmHttp(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleClearAlarmHttp, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + j.dump());
    try {
        WashCarOperation operation(j);
        operation.setReqSource("http");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        if (!operation.isValid()) {
            return {400, R"({"error":"Invalid parameters"})"};
        }
        
        auto result = lobbyService.clearAlarmHttp(operation);
        if (!result.success) {
            json err;
            err["error"] = result.message;
            return {500, err.dump()};
        }

        json resp;
        resp["m0"] = 0;
        resp["m100"] = 1;
        resp["plcId"] = "fx_plc_01";
        resp["time"] = getCurrentTimeStr();
        resp["faults"] = json::array({{{"code", 0}, {"msg", ""}}});

        return {200, resp.dump()};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP clear alarm exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarPoint(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarPoint, payload=") + j.dump());
    try {
        if (j.contains("point on")) {
            return handlePointOnHttp(j, productId, deviceNum);
        } else if (j.contains("point off")) {
            return handlePointOffHttp(j, productId, deviceNum);
        } else if (j.contains("point reset")) {
            return handlePointResetHttp(j, productId, deviceNum);
        }
        return {400, R"({"error":"Invalid parameters"})"};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar point exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}

HttpResponse HTTPCommandController::handleWashCarQuery(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    LOG_INFO(std::string("[HTTP] handleWashCarQuery, payload=") + j.dump());
    try {
        if (j.contains("point stauts")) {
            return handlePointStatusHttp(j, productId, deviceNum);
        }
        return {400, R"({"error":"Invalid parameters"})"};
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("HTTP washcar query exception: ") + e.what());
        return {500, R"({"error":"Internal server error"})"};
    }
}
