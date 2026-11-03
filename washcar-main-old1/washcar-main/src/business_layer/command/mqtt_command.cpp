#include "business_layer/command/mqtt_command.h"
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_response.h"
#include "common/config/config_manager.h"  // [2026-08-26] 读 cfg.box().boxType 做海天/驿公里路由
#include <nlohmann/json.hpp>
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"   // [TEMP_DIAG] cmdId + nowMs
#include <sstream>
#include <vector>

static void parseTopic(const std::string& topic, std::string& productId, std::string& deviceNum) {
    std::stringstream ss(topic);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            parts.push_back(segment);
        }
    }
    if (parts.size() >= 2) {
        productId = parts[0];
        deviceNum = parts[1];
    }
}

static bool parseFlexemTopic(const std::string& topic, std::string& boxNo, std::string& endpoint) {
    std::vector<std::string> parts;
    std::string segment;

    char separator = topic.find('/') != std::string::npos ? '/' : '.';
    std::stringstream ss(topic);
    while (std::getline(ss, segment, separator)) {
        if (!segment.empty()) {
            parts.push_back(segment);
        }
    }

    if (parts.size() == 3 && parts[0] == "flexem") {
        boxNo = parts[1];
        endpoint = parts[2];
        return true;
    }
    return false;
}

static void fillMqttDefaults(json& j, const std::string& boxNo) {
    if (!boxNo.empty() && (!j.contains("boxNo") || j["boxNo"].is_null() || j.value("boxNo", "").empty())) {
        j["boxNo"] = boxNo;
    }
    if (!j.contains("plcId") || j["plcId"].is_null() || j.value("plcId", "").empty()) {
        j["plcId"] = "fx_plc_01";
    }
    j["reqSource"] = "mqtt";
}

static void addReadPoints(json& j) {
    json points = json::array();
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.key();
        if (key == "boxNo" || key == "time" || key == "plcId" || key == "reqSource" || key == "readdata") {
            continue;
        }
        points.push_back(key);
    }

    if (points.empty()) {
        // 云端没指定点位就回全部属性。点位清单来自洗车机点位表(属性类 → readdata),
        // 不在这里写死点位,换机型不用改这里。
        const auto& pointTable = ConfigManager::instance().machine();
        for (const auto& signal : pointTable.signals) {
            if (signal.report == washcar::cfg::SignalReport::REPORT_PROPERTY) {
                points.push_back(signal.point);
            }
        }
    }
    j["points"] = points;
}

MQTTCommandController::MQTTCommandController(ILobbyService& lobby) : lobbyService(lobby) {}

HttpResponse MQTTCommandController::handle(const std::string& topic, const std::string& payload) {
    return HttpResponse{}; 
}

void MQTTCommandController::handleMqtt(const std::string& topic, const std::string& payload) {
    // 顶层防御: 任何异常( JSON 解析失败、字段缺失、类型不匹配、stdexcept 等)
    // 都必须被吞掉, 否则会 std::terminate 整进程退出。
    // 错误原因记到日志, 消息丢弃, MQTT 链路继续存活。
    try {
        // [TEMP_DIAG] 给整条同步命令链打 cmdId (一次 nowMs 复用),
        // 下游 lobby/washcar/fx_plc/detection 的 LOG_INFO 都能透传。
        const long long recvMs = trace::nowMs();
        trace::cmdId = std::to_string(recvMs);

        if (payload.empty()) {
            LOG_WARNING(std::string("[MQTT] 收到空 payload, topic=") + topic);
            return;
        }

        auto j = json::parse(payload);
        std::string flexemBoxNo;
        std::string flexemEndpoint;
        if (parseFlexemTopic(topic, flexemBoxNo, flexemEndpoint)) {
            fillMqttDefaults(j, flexemBoxNo);

            if (flexemEndpoint == FLEXEM_COMMAND_TOPIC) {
                // 按 cfg.boxType 路由海天/驿公里子分支
                const std::string boxType = ConfigManager::instance().box().boxType;
                if (boxType == "yiGongLi") {
                    // ===== 驿公里分支(boxType == "yiGongLi") =====
                    if (j.contains("Manual start") && j["Manual start"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarStart (yiGongLi), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarStart(j, "flexem", flexemBoxNo);
                    } else if (j.contains("washcar stop") && j["washcar stop"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarStop (yiGongLi), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarStop(j, "flexem", flexemBoxNo);
                    } else if (j.contains("machine abort") && j["machine abort"] == 1) {
                        // 驿公里 m206 中止洗车 - 走专用 abortWashCar 虚函数(下发 m206,非 m205)
                        LOG_INFO(std::string("[MQTT] dispatch Flexem AbortWashCar (yiGongLi m206), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleAbortWashCar(j, "flexem", flexemBoxNo);
                    } else if (j.contains("reset safely") && j["reset safely"] == 1) {
                        // 驿公里 m203 安全复位 - 走专用 resetSafe 虚函数
                        LOG_INFO(std::string("[MQTT] dispatch Flexem ResetSafe (yiGongLi m203), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleResetSafe(j, "flexem", flexemBoxNo);
                    } else if (j.contains("reset") && j["reset"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarReset (yiGongLi), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarReset(j, "flexem", flexemBoxNo);
                    } else if (j.contains("stop service") && j["stop service"] == 1) {
                        // 驿公里 m510 停止营业 2 - 走专用 stopService 虚函数
                        LOG_INFO(std::string("[MQTT] dispatch Flexem StopService (yiGongLi m510), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleStopService(j, "flexem", flexemBoxNo);
                    } else if (j.contains("stop work") && j["stop work"] == 1) {
                        // 驿公里 m501 停止营业 - 走专用 stopWork 虚函数
                        LOG_INFO(std::string("[MQTT] dispatch Flexem StopWork (yiGongLi m501), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleStopWork(j, "flexem", flexemBoxNo);
                    } else {
                        LOG_WARNING(std::string("[MQTT] unknown yiGongLi Flexem command, topic=") + topic
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs)
                            + ", payload=" + payload);
                    }
                } else {
                    // ===== 海天分支(默认 haiTian,boxType != "yiGongLi") =====
                    if (j.contains("Manual start") && j["Manual start"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarStart (haiTian), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarStart(j, "flexem", flexemBoxNo);
                    } else if (j.contains("stop") && j["stop"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarStop (haiTian), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarStop(j, "flexem", flexemBoxNo);
                    } else if (j.contains("reset") && j["reset"] == 1) {
                        LOG_INFO(std::string("[MQTT] dispatch Flexem WashCarReset (haiTian), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleWashCarReset(j, "flexem", flexemBoxNo);
                    } else if (j.contains("machine abort") && j["machine abort"] == 1) {
                        // 驿公里独有命令,海天 word 协议无 m206,直接返 unsupported
                        LOG_INFO(std::string("[MQTT] dispatch Flexem AbortWashCar (haiTian, unsupported), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleAbortWashCar(j, "flexem", flexemBoxNo);
                    } else if (j.contains("stop work") && j["stop work"] == 1) {
                        // 驿公里独有命令 m501,海天不支持
                        LOG_INFO(std::string("[MQTT] dispatch Flexem StopWork (haiTian, unsupported), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleStopWork(j, "flexem", flexemBoxNo);
                    } else if (j.contains("stop service") && j["stop service"] == 1) {
                        // 驿公里独有命令 m510,海天不支持
                        LOG_INFO(std::string("[MQTT] dispatch Flexem StopService (haiTian, unsupported), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleStopService(j, "flexem", flexemBoxNo);
                    } else if (j.contains("reset safely") && j["reset safely"] == 1) {
                        // 驿公里独有命令 m203,海天不支持
                        LOG_INFO(std::string("[MQTT] dispatch Flexem ResetSafe (haiTian, unsupported), boxNo=") + flexemBoxNo
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs));
                        handleResetSafe(j, "flexem", flexemBoxNo);
                    } else {
                        LOG_WARNING(std::string("[MQTT] unknown haiTian Flexem command, topic=") + topic
                            + ", cmdId=" + trace::cmdId
                            + ", recvMs=" + std::to_string(recvMs)
                            + ", payload=" + payload);
                    }
                }
                return;
            }

            if (flexemEndpoint == FLEXEM_READ_TOPIC) {
                addReadPoints(j);
                LOG_INFO(std::string("[MQTT] dispatch Flexem ReadData, boxNo=") + flexemBoxNo
                    + ", cmdId=" + trace::cmdId
                    + ", recvMs=" + std::to_string(recvMs));
                handleWashCarReadData(j, "flexem", flexemBoxNo);
                return;
            }

            if (flexemEndpoint == FLEXEM_REBOOT_TOPIC) {
                if (j.contains("reboot") && j["reboot"] == 1) {
                    LOG_INFO(std::string("[MQTT] dispatch Flexem Reboot, boxNo=") + flexemBoxNo
                        + ", cmdId=" + trace::cmdId
                        + ", recvMs=" + std::to_string(recvMs));
                    lobbyService.handleFlexemReboot(flexemBoxNo);
                } else {
                    LOG_WARNING(std::string("[MQTT] Flexem reboot payload invalid, ignored, boxNo=") + flexemBoxNo
                        + ", cmdId=" + trace::cmdId
                        + ", recvMs=" + std::to_string(recvMs)
                        + ", payload=" + payload);
                }
                return;
            }
        }

        if(topic == UPDATE_CONFIG_TOPIC ){
            LOG_INFO(std::string("[MQTT] dispatch update-config, payload=") + payload);
            handleUpdateConfig( j);
        } else if(topic == GET_ALL_DEVICE_STATUS_TOPIC){
            LOG_INFO(std::string("[MQTT] dispatch get-all-device-status, payload=") + payload);
            handleGetAllDeviceStatus( j);
        } else if(topic.find("/function/get") != std::string::npos) {
            std::string productId, deviceNum;
            parseTopic(topic, productId, deviceNum);

            if (j.contains("Manual start") && j["Manual start"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch WashCarStart, productId=") + productId + ", deviceNum=" + deviceNum + ", cmdId=" + trace::cmdId + ", recvMs=" + std::to_string(recvMs));
                handleWashCarStart(j, productId, deviceNum);
            } else if (j.contains("washcar stop") && j["washcar stop"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch WashCarStop, productId=") + productId + ", deviceNum=" + deviceNum + ", cmdId=" + trace::cmdId + ", recvMs=" + std::to_string(recvMs));
                handleWashCarStop(j, productId, deviceNum);
            } else if (j.contains("point on") && j["point on"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch PointOn, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + payload);
                handlePointOnMqtt(j, productId, deviceNum);
            } else if (j.contains("point off") && j["point off"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch PointOff, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + payload);
                handlePointOffMqtt(j, productId, deviceNum);
            } else if (j.contains("point reset") && j["point reset"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch PointReset, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + payload);
                handlePointResetMqtt(j, productId, deviceNum);
            } else if (j.contains("point stauts") && j["point stauts"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch PointStatus, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + payload);
                handlePointStatusMqtt(j, productId, deviceNum);
            } else if (j.contains("alarm clear") && j["alarm clear"] == 1) {
                LOG_INFO(std::string("[MQTT] dispatch ClearAlarm, productId=") + productId + ", deviceNum=" + deviceNum + ", payload=" + payload);
                handleClearAlarmMqtt(j, productId, deviceNum);
            } else {
                LOG_WARNING(std::string("[MQTT] no handler matched, topic=") + topic + ", payload=" + payload);
            }
        }
    } catch (const json::parse_error& e) {
        // payload 末尾有 \n、空 payload、非 JSON 等情况会走这里
        LOG_ERROR(std::string("[MQTT] JSON 解析失败, topic=") + topic
                  + ", cmdId=" + trace::cmdId
                  + ", payload=" + payload
                  + ", err=" + e.what());
    } catch (const std::exception& e) {
        // 兜底: j.contains / j[...] 类型不匹配、WashCarOperation 构造异常等
        LOG_ERROR(std::string("[MQTT] handleMqtt 异常, topic=") + topic
                  + ", cmdId=" + trace::cmdId
                  + ", payload=" + payload
                  + ", err=" + e.what());
    } catch (...) {
        LOG_ERROR(std::string("[MQTT] handleMqtt 未知异常, topic=") + topic
                  + ", cmdId=" + trace::cmdId
                  + ", payload=" + payload);
    }
}


void MQTTCommandController::handleUpdateConfig(const nlohmann::json& j){
    
} 

void MQTTCommandController::handleGetAllDeviceStatus(const nlohmann::json& j){

}

//void MQTTCommandController::handleOperateWithVerify(const nlohmann::json& j){
//    try {
//        SolenoidValveOperation operation(j);
//        if (!operation.isValid()) {
//            LOG_ERROR("SolenoidValveOperation: 缺少必要字段");
//            return;
//        }
//        auto result = lobbyService.operateSolenoidValve(operation);
//        if (!result.success) {
//            LOG_ERROR("Operate solenoid valve 失败");
//        }
//    } catch (const std::exception& e) {
//        LOG_ERROR(std::string("Exception: ") + e.what());
//    }
//}
/*
void MQTTCommandController::handleOperateWithVerify(const nlohmann::json& j){
    try {
       
        SolenoidValveOperation operation(j);

        if (operation.isValid()) {
            LOG_ERROR(" SolenoidValveOperation: cmd 为空");
            return;
        }

        auto result = lobbyService.operateSolenoidValve(operation);

        if (!result.success) {
            LOG_ERROR("Operate solenoid valve 失败");
            return;
        }

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Exception: ") + e.what());
    }
}*/

void MQTTCommandController::handleWashCarStart(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.startWashCar(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT washcar start exception: ") + e.what());
    }
}

void MQTTCommandController::handleWashCarStop(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.stopWashCar(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT washcar stop exception: ") + e.what());
    }
}

void MQTTCommandController::handleWashCarReset(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.resetWashCar(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT washcar reset exception: ") + e.what());
    }
}

void MQTTCommandController::handleWashCarChangeMode(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.changeMode(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT washcar changeMode exception: ") + e.what());
    }
}

void MQTTCommandController::handleWashCarReadData(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.getWashCarStatus(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT washcar readData exception: ") + e.what());
    }
}

void MQTTCommandController::handlePointOnMqtt(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.pointOnMqtt(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT point on exception: ") + e.what());
    }
}

void MQTTCommandController::handlePointOffMqtt(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.pointOffMqtt(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT point off exception: ") + e.what());
    }
}

void MQTTCommandController::handlePointResetMqtt(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.pointResetMqtt(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT point reset exception: ") + e.what());
    }
}

void MQTTCommandController::handlePointStatusMqtt(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);
        
        auto result = lobbyService.pointStatusMqtt(operation);
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT point status exception: ") + e.what());
    }
}

void MQTTCommandController::handleClearAlarmMqtt(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        auto result = lobbyService.clearAlarmMqtt(operation);

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT clear alarm exception: ") + e.what());
    }
}

// =====================================================================
// 驿公里独有命令 handle(海天那边调也走这里,但 lobby.abortWashCar 等会返 unsupported 给海天实装)
// =====================================================================

void MQTTCommandController::handleAbortWashCar(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        auto result = lobbyService.abortWashCar(operation);

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT abortWashCar exception: ") + e.what());
    }
}

void MQTTCommandController::handleStopWork(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        auto result = lobbyService.stopWork(operation);

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT stopWork exception: ") + e.what());
    }
}

void MQTTCommandController::handleStopService(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        auto result = lobbyService.stopService(operation);

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT stopService exception: ") + e.what());
    }
}

void MQTTCommandController::handleResetSafe(const nlohmann::json& j, const std::string& productId, const std::string& deviceNum) {
    try {
        WashCarOperation operation(j);
        operation.setReqSource("mqtt");
        operation.setProductId(productId);
        operation.setDeviceNum(deviceNum);

        auto result = lobbyService.resetSafe(operation);

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("MQTT resetSafe exception: ") + e.what());
    }
}
