#include "business_layer/lobby/lobby_service.h"
#include "business_layer/lobby/lobby_object.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "common/log/log_manager.h"
#include "common/log/trace_context.h"   // [TEMP_DIAG] cmdId + nowMs
#include "common/config/config_manager.h"   // 上报轮询间隔可配
#include "data_layer/camera/camera_info.h"
#include "data_layer/camera/camera_real_time_frame.h"
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cctype>
#include <cstdint>
#include "nlohmann/json.hpp"

static std::string getCurrentTimeStr() {
    time_t now = time(nullptr);
    tm* tm_now = localtime(&now);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_now->tm_year + 1900,
             tm_now->tm_mon + 1,
             tm_now->tm_mday,
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec);
    return std::string(buf);
}

static std::int64_t getCurrentTimeSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

static nlohmann::json buildFlexemCommandReply() {
    nlohmann::json msg;
    msg["message"] = "success";
    msg["time"] = getCurrentTimeSeconds();
    return msg;
}

static std::string toFlexemPointName(const std::string& key) {
    std::string result = key;
    for (auto& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}
//public
LobbyService::LobbyService(ISafetyService& safetyService, ICommandService& commandService, IDeviceService& deviceService, IWashCarService& washcarService, ITimer& timer, DeviceStatusCache& deviceStatusCache, IDetectionService& detectionService)
    : m_safetykService(safetyService), m_commandService(commandService), m_deviceService(deviceService), m_washcarService(washcarService), m_Timer(timer), deviceStatusCache_(deviceStatusCache), m_detectionService(detectionService)
{
    startDailyReportMonitor();
    startValueChangeMonitor();
    startFaultMonitor();

    // 定时驱动 AI 检测(频率由检测服务内部按区域模式决定)
    startAIDetectionMonitor();
}


LobbyResult<BoxDeviceStatus> LobbyService::retrieveDeviceStatus(const DeviceStatusQuery& query){
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<BoxDeviceStatus>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    if(query.getReqSource() == "http"){
        //去拿设备状态  
        BoxDeviceStatus status = m_deviceService.viewAllDeviceStatus();
        return LobbyResult<BoxDeviceStatus>::Ok(status);
    } else{
        std::cout << "query.getReqSource:" << query.getReqSource() << std::endl;
        return LobbyResult<BoxDeviceStatus>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
}


LobbyResult<FrameQuery> LobbyService::retrieveLiveCameraFrame(const FrameQuery& query)
{
    try {
        auto auth = m_safetykService.authenticate();
        if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<FrameQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

        CameraInfo cameraInfo(query.getCameraId());
        CameraRealTimeFrame frame = m_deviceService.getCameraRealTimeFrame(cameraInfo);

        if (!frame.isValid()) {
            return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }

        std::vector<uint8_t> bmpData = frame.getBmpData();
        if (bmpData.empty()) {
            return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }

        FrameQuery result(
            query.getCameraId(),
            bmpData,
            frame.getWidth(),
            frame.getHeight(),
            frame.getTimestamp()
        );

        return LobbyResult<FrameQuery>::Ok(result);
    } catch (const std::exception& e) {
        LOG_WARNING("获取实时摄像头帧出错: " + std::string(e.what()));
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
}

LobbyResult<FrameQuery> LobbyService::retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) {
    try {
        auto auth = m_safetykService.authenticate();
        if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<FrameQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    } catch (const std::exception& e) {
        LOG_WARNING("获取历史摄像头视频出错: " + std::string(e.what()));
        return LobbyResult<FrameQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
}

LobbyResult<SensorQuery> LobbyService::retrieveAlarmRecords(const AlarmQuery& query)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    // 查看报警记录
    //1.正确
    return LobbyResult<SensorQuery>::Ok(SensorQuery());
    //2.错误
    // return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    //3.异常错误
}


LobbyResult<SensorQuery> LobbyService::downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    // 上传视频的逻辑 
    //1.去device服务里面去验证这个视频的状态是否可用
    //2.命令服务创建命令
    //3.命令调用命令服务，加入到命令服务的接口
    //4.打开电磁阀
    //5.根据返回值进行更改命令的状态

    //1.正确
    return LobbyResult<SensorQuery>::Ok(SensorQuery());
    //2.错误
    // return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    //3.异常错误
}

LobbyResult<SolenoidValveOperationResult> LobbyService::operateFxSolenoidValve(const std::string& plcId, int yOctal, const std::string& cmd) {
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    bool success = false;
    if (cmd == "open") {
        std::cout<<"open";
        //success = m_deviceService.openFxSolenoid(plcId, yOctal);
    } else if (cmd == "close") {
        std::cout<<"clode";
        //success = m_deviceService.closeFxSolenoid(plcId, yOctal);
    } else {
        return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    // 构造结果对象：deviceId, plc, code, message
    std::string deviceId = "Y" + std::to_string(yOctal);
    int code = success ? 0 : -1;
    std::string message = success ? "Success" : "Fail";
    SolenoidValveOperationResult result(deviceId, plcId, code, message);

    return LobbyResult<SolenoidValveOperationResult>::Ok(result);
}

LobbyResult<SolenoidValveOperationResult> LobbyService::operateSolenoidValve(const SolenoidValveOperation& operation)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    try {
        // 假设 operation.getDeviceId() 返回 Y 点的八进制数字符串，operation.getPlcId() 返回 PLC ID
        int yOctal = std::stoi(operation.getDeviceId());
        // bool turnOn = (operation.getCmd() == "open");
        // bool success = m_deviceService.operateFxSolenoid(operation.getPlcId(), yOctal, turnOn);
        
        //改为fx plc
        bool success = false;
        if (operation.getCmd() == "open") {
            std::cout<<"open";
            //success = m_deviceService.openFxSolenoid(operation.getPlcId(), yOctal);
        } else if (operation.getCmd() == "close") {
            std::cout<<"close";
            //success = m_deviceService.closeFxSolenoid(operation.getPlcId(), yOctal);
        } else {
            return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }

        // 创建命令记录（可选）
        DeviceOperationResult result(success ? 0 : -1, success ? "Success" : "Fail");
        Command cmd = Command::createOperateSolenoidResult(result, operation);
        m_commandService.executeCommand(cmd);
        
        // 发送 MQTT 结果
        sendSolenoidResult(cmd, operation, result);
        
        SolenoidValveOperationResult opResult = SolenoidValveOperationResult::createResult(result, operation);
        return LobbyResult<SolenoidValveOperationResult>::Ok(opResult);
    } catch (const std::exception& e) {
        LOG_WARNING("操作电磁阀出错 " + std::string("Exception: ") + e.what());
        return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
}


/*
LobbyResult<SolenoidValveOperationResult> LobbyService::operateSolenoidValve(const SolenoidValveOperation& operation)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    //这里需要检查设备状态的

    try {

        PlcDeviceInfo plcDevice = PlcDeviceInfo::createPlcDevice(operation);

        DeviceOperationResult result;

        if(operation.getCmd() == "open"){
            result = m_deviceService.openSolenoidValue(plcDevice);
        }
        else if(operation.getCmd() == "close"){
            result = m_deviceService.closeSolenoidValue(plcDevice); 
        } 
        else {
            return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }

        Command cmd = Command::createOperateSolenoidResult(result,operation);

        m_commandService.executeCommand(cmd);
        
        sendSolenoidResult(cmd,operation,result);
        
        
        SolenoidValveOperationResult opResult = SolenoidValveOperationResult::createResult(result,operation);

        return LobbyResult<SolenoidValveOperationResult>::Ok(opResult);
    } catch (const std::exception& e) {
            LOG_WARNING("操作电磁阀出错 " + std::string("Exception: ") + e.what());
            return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);

        }
}
*/

LobbyResult<WashCarOperationResult> LobbyService::startWashCar(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: startWashCar" << std::endl;

    const long long startEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=startWashCar")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=startWashCar result=deny reason=auth_denied, boxNo=" + operation.getBoxNo()); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    if (!operation.isValid()) {
        LOG_WARNING(std::string("[LobbyService] reject startWashCar: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
        LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=startWashCar result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    // AI 检测前置检查
    std::string cameraId = operation.getCameraId();
    if (!checkDetectionBeforeWashCar(cameraId)) {
        LOG_WARNING(std::string("[LobbyService] reject startWashCar: AI detection failed, cameraId=") + cameraId);
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);//, "AI检测异常，禁止洗车"
    }

    // 频率切换是 AI 检测结果驱动的状态机, 洗车命令不再直接切频
    // (车进入区域时低频轮会自动升频)

    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=startWashCar result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - startEnterMs));
    WashCarOperationResult result = m_washcarService.startWashCar(operation);

    if (result.getCode() == ErrorCode::Code::SUCCESS) {
        startWashCarStatusMonitor(operation);
    }

    Command cmd = Command::createWashCarStart(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            auto msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=startWashCar")
                + ", topicSuffix=" + WASHCAR_START_REPORT_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - startEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_START_REPORT_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=startWashCar")
                + ", topicSuffix=" + WASHCAR_START_REPORT_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - startEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_START_REPORT_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt start failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_START_REPORT_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=startWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - startEnterMs));
    std::cout << "[LobbyService] finished: startWashCar" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::stopWashCar(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: stopWashCar" << std::endl;

    const long long stopEnterMs = trace::nowMs();   // [TEMP_DIAG]
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=stopWashCar")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopWashCar result=deny reason=auth_denied, boxNo=" + operation.getBoxNo()); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    if (!operation.isValid()) {
        LOG_WARNING(std::string("[LobbyService] reject stopWashCar: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
        LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopWashCar result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    // 恢复空闲模式
    setAIDetectionIdleMode(operation.getCameraId());


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=stopWashCar result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - stopEnterMs));
    WashCarOperationResult result = m_washcarService.stopWashCar(operation);

    if (result.getCode() == ErrorCode::Code::SUCCESS) {
        stopWashCarStatusMonitor();
    }

    Command cmd = Command::createWashCarStop(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::WASHCAR_STOPPED) {
            auto msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=stopWashCar")
                + ", topicSuffix=" + WASHCAR_STOP_REPORT_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_STOP_REPORT_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=stopWashCar")
                + ", topicSuffix=" + WASHCAR_STOP_REPORT_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_STOP_REPORT_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt stop failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_STOP_REPORT_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopEnterMs));
    std::cout << "[LobbyService] finished: stopWashCar" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::resetWashCar(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: resetWashCar" << std::endl;

    const long long resetEnterMs = trace::nowMs();   // [TEMP_DIAG]
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=resetWashCar")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=resetWashCar result=deny reason=auth_denied, boxNo=" + operation.getBoxNo()); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    if (!operation.isValid()) {
        LOG_WARNING(std::string("[LobbyService] reject resetWashCar: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
        LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=resetWashCar result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    // 恢复空闲模式
    setAIDetectionIdleMode(operation.getCameraId());


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=resetWashCar result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - resetEnterMs));
    WashCarOperationResult result = m_washcarService.resetWashCar(operation);

    Command cmd = Command::createWashCarReset(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            auto msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=resetWashCar")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - resetEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=resetWashCar")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - resetEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt reset failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_POINT_POST_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=resetWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - resetEnterMs));
    std::cout << "[LobbyService] finished: resetWashCar" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

// =====================================================================
// 驿公里独有命令(中转实现, 对称 resetWashCar 结构)
// 海天实装返 unsupported, 驿公里实装 forceFxM 真下发
// =====================================================================

LobbyResult<WashCarOperationResult> LobbyService::abortWashCar(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: abortWashCar" << std::endl;

    const long long abortEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=abortWashCar")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    if (operation.getReqSource() == "mqtt") {
        if (!m_safetykService.authenticate()) {
            LOG_WARNING("[LobbyService] abortWashCar auth denied");
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=abortWashCar result=deny reason=auth_denied, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
        }
        if (!operation.isValid()) {
            LOG_WARNING(std::string("[LobbyService] reject abortWashCar: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=abortWashCar result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=abortWashCar result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - abortEnterMs));
    WashCarOperationResult result = m_washcarService.abortWashCar(operation);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            json msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=abortWashCar")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - abortEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=abortWashCar")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - abortEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt abortWashCar failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_POINT_POST_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=abortWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - abortEnterMs));
    std::cout << "[LobbyService] finished: abortWashCar" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::stopWork(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: stopWork" << std::endl;

    const long long stopWorkEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=stopWork")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    if (operation.getReqSource() == "mqtt") {
        if (!m_safetykService.authenticate()) {
            LOG_WARNING("[LobbyService] stopWork auth denied");
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopWork result=deny reason=auth_denied, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
        }
        if (!operation.isValid()) {
            LOG_WARNING(std::string("[LobbyService] reject stopWork: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopWork result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=stopWork result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - stopWorkEnterMs));
    WashCarOperationResult result = m_washcarService.stopWork(operation);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            json msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=stopWork")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopWorkEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=stopWork")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopWorkEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt stopWork failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_POINT_POST_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopWork")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopWorkEnterMs));
    std::cout << "[LobbyService] finished: stopWork" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::stopService(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: stopService" << std::endl;

    const long long stopServiceEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=stopService")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    if (operation.getReqSource() == "mqtt") {
        if (!m_safetykService.authenticate()) {
            LOG_WARNING("[LobbyService] stopService auth denied");
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopService result=deny reason=auth_denied, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
        }
        if (!operation.isValid()) {
            LOG_WARNING(std::string("[LobbyService] reject stopService: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=stopService result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=stopService result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - stopServiceEnterMs));
    WashCarOperationResult result = m_washcarService.stopService(operation);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            json msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=stopService")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopServiceEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=stopService")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - stopServiceEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt stopService failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_POINT_POST_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopService")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopServiceEnterMs));
    std::cout << "[LobbyService] finished: stopService" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::resetSafe(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: resetSafe" << std::endl;

    const long long resetSafeEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=resetSafe")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", plcId=" + operation.getPlcId()
        + ", cmdId=" + trace::cmdId);

    if (operation.getReqSource() == "mqtt") {
        if (!m_safetykService.authenticate()) {
            LOG_WARNING("[LobbyService] resetSafe auth denied");
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=resetSafe result=deny reason=auth_denied, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
        }
        if (!operation.isValid()) {
            LOG_WARNING(std::string("[LobbyService] reject resetSafe: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
            LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=resetSafe result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
            return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
        }
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=resetSafe result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", mode=" + operation.getMode()
        + ", point=" + operation.getPoint()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - resetSafeEnterMs));
    WashCarOperationResult result = m_washcarService.resetSafe(operation);

    if (operation.getReqSource() == "mqtt") {
        if (result.getCode() == ErrorCode::Code::SUCCESS) {
            json msg = buildFlexemCommandReply();
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=resetSafe")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", message=success"
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - resetSafeEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=resetSafe")
                + ", topicSuffix=" + WASHCAR_POINT_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - resetSafeEnterMs));
        } else {
            json failMsg;
            failMsg["message"] = result.getMessage();
            failMsg["code"] = result.getCode();
            failMsg["time"] = getCurrentTimeSeconds();
            // [B-NOT-YET-SENT] m_commandService.sendWashCarReportToMqtt(WASHCAR_POINT_POST_TOPIC, failMsg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_WARNING(std::string("[LobbyService] mqtt resetSafe failed, no commandreply, message=") + result.getMessage()
                + " (failMsg=" + failMsg.dump() + " ready to send, topic=" + "WASHCAR_POINT_POST_TOPIC");
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=resetSafe")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - resetSafeEnterMs));
    std::cout << "[LobbyService] finished: resetSafe" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::getWashCarStatus(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: getWashCarStatus" << std::endl;

    const long long readEnterMs = trace::nowMs();   // [TEMP_DIAG] 给 lobbyMs 算 diff
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=getWashCarStatus")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", cmdId=" + trace::cmdId);

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=getWashCarStatus result=deny reason=auth_denied, boxNo=" + operation.getBoxNo()); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    if (!operation.isValid()) {
        LOG_WARNING(std::string("[LobbyService] reject getWashCarStatus: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
        LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=getWashCarStatus result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=getWashCarStatus result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource()
        + ", cmdId=" + trace::cmdId
        + ", lobbyMs=" + std::to_string(trace::nowMs() - readEnterMs));
    WashCarOperationResult result = m_washcarService.getWashCarStatus(operation);

    // 读取状态暂时不写入日志
    
    if ((operation.getReqSource() == "mqtt" || operation.getReqSource() == "http") 
        && !operation.getProductId().empty() && !operation.getDeviceNum().empty()) {
        json msg;
        const auto& status = result.getStatus();
        for (const auto& [key, value] : status) {
            msg[toFlexemPointName(key)] = value;
        }
        msg["boxNo"] = result.getBoxNo();
        msg["time"] = getCurrentTimeSeconds();
        
        if (operation.getReqSource() == "mqtt") {
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_ENTER cmd=getWashCarStatus")
                + ", topicSuffix=" + WASHCAR_INFO_POST_TOPIC
                + ", pointCount=" + std::to_string(msg.size())
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - readEnterMs));
            m_commandService.sendWashCarReportToMqtt(WASHCAR_INFO_POST_TOPIC, msg.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);
            LOG_INFO(std::string("[LobbyService] MQTT_REPLY_SEND_EXIT cmd=getWashCarStatus")
                + ", topicSuffix=" + WASHCAR_INFO_POST_TOPIC
                + ", cmdId=" + trace::cmdId
                + ", elapsedMs=" + std::to_string(trace::nowMs() - readEnterMs));
        }
    }

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=getWashCarStatus")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - readEnterMs));
    std::cout << "[LobbyService] finished: getWashCarStatus" << std::endl;
    
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::clearAlarm(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: clearAlarm" << std::endl;

    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=clearAlarm")
        + ", reqSource=" + operation.getReqSource()
        + ", boxNo=" + operation.getBoxNo()
        + ", plcId=" + operation.getPlcId());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=clearAlarm result=deny reason=auth_denied, boxNo=" + operation.getBoxNo()); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }

    if (!operation.isValid()) {
        LOG_WARNING(std::string("[LobbyService] reject clearAlarm: invalid operation, reqSource=") + operation.getReqSource() + ", boxNo=" + operation.getBoxNo());
        LOG_WARNING("[LobbyService] LOBBY_DECISION cmd=clearAlarm result=deny reason=invalid_operation, boxNo=" + operation.getBoxNo());
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }


    LOG_INFO(std::string("[LobbyService] LOBBY_DECISION cmd=clearAlarm result=allow")
        + ", boxNo=" + operation.getBoxNo()
        + ", reqSource=" + operation.getReqSource());
    WashCarOperationResult result = m_washcarService.clearAlarm(operation);

    Command cmd = Command::createClearAlarm(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        sendWashCarResult(cmd, operation, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_ALARM_CLEAR_RESULT);
    }

    std::cout << "[LobbyService] finished: clearAlarm" << std::endl;

    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<bool> LobbyService::changeMode(const WashCarOperation& operation)
{
    std::cout << "[LobbyService] execute: changeMode" << std::endl;

    bool success = m_washcarService.changeMode(operation);
    if (success) {
        std::cout << "[LobbyService] finished: changeMode" << std::endl;
        return LobbyResult<bool>::Ok(true);
    }

    Command cmd = Command::createChangeMode(operation);
    m_commandService.executeCommand(cmd);

    std::cout << "[LobbyService] finished: changeMode" << std::endl;

    return LobbyResult<bool>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
}

LobbyResult<void> LobbyService::startDeviceStatusUpload(const DeviceStatusQuery& query){
    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<void>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    if(!TimingUpload()){
        return LobbyResult<void>::Error(ErrorCode::Code::SERVER_TOO_MANY_Repeat);
    }
    return LobbyResult<void>::Ok();
}

//private
void LobbyService::TimingProcessing(){
    


}

bool LobbyService::TimingUpload(){

    if(m_Timer.isRunningUpload()) return false;
    m_Timer.scheduleRepeated(5000,[this]() {
        // 👇 业务逻辑在这里（不是 Timer 内部）
        auto status = m_deviceService.viewAllDeviceStatus();
        // upload(status); //上传逻辑
        sendBoxStatusResult(status);
        
    });
    return true;
}



void LobbyService::TimingPullVideoFrame(){

}

void LobbyService::sendBoxStatusResult(const BoxDeviceStatus& boxStatus) {
    json msg;

    // 从传入的 boxStatus 中获取所有设备状态
    // msg["solenoid"] = boxStatus.getSolenoidStatusList();
    // msg["camera"] = boxStatus.getCameraStatusList();
    // msg["infrared"] = boxStatus.getInfraredSensorStatusList();
    // msg["smokeDetector"] = boxStatus.getSmokeDetectorStatusList();
    // msg["waterLevel"] = boxStatus.getWaterLevelSensorStatusList();

    // MQTT 发送
    std::string topic = "device/uploadStatus/result";
    m_commandService.sendCommandResultToMqtt(topic, msg.dump(), MessageBuffer::MessageType::TYPE_VALUE_CHANGE);
}


void LobbyService::sendSolenoidResult(const Command& cmd, const SolenoidValveOperation& op,const DeviceOperationResult& result){
    if(op.getReqSource() == "http") return;
    json msg;
    msg["cmdId"] = cmd.getCmdId();
    msg["deviceId"] = cmd.getDeviceId();
    msg["plcId"] = op.getPlcId();
    msg["success"] = result.operationBool()? "0" : "-1";

    std::string topic = "device/solenoid/result";

    m_commandService.sendCommandResultToMqtt(topic, msg.dump(), MessageBuffer::MessageType::TYPE_SOLENOID_RESULT);
}

void LobbyService::sendWashCarResult(const Command& cmd, const WashCarOperation& op, const WashCarOperationResult& result, const std::string& topicSuffix, MessageBuffer::MessageType msgType) {
    json msg;
    std::string pointName = op.getPoint();
    int pointValue = result.getCode() == ErrorCode::Code::SUCCESS ? 1 : 0;
    msg[pointName] = pointValue;
    msg["plcId"] = "fx_plc_01";
    msg["time"] = getCurrentTimeStr();
    msg["faults"] = json::array({{{"code", 0}, {"msg", ""}}});

    m_commandService.sendWashCarReportToMqtt(topicSuffix, msg.dump(), msgType);
}

void LobbyService::startWashCarStatusMonitor(const WashCarOperation& operation) {
    m_washcarService.startWashCarStatusMonitor(
        operation,
        [this, &operation](const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendWashCarReportToMqtt(topicSuffix, payload, type);

            // 检测洗车完成，自动恢复空闲模式
            try {
                auto j = nlohmann::json::parse(payload);
                int code = j.value("code", 0);
                int currentStep = j.value("d142", 0);

                // code == 0 && currentStep == 10 → 完成
                // code != 0 → 错误
                // code == WASHCAR_STOPPED → 停止
                if ((code == 0 && currentStep == 10) || code == -1 || code == static_cast<int>(ErrorCode::Code::WASHCAR_STOPPED)) {
                    std::string statusStr;
                    if (code == 0 && currentStep == 10) {
                        statusStr = "completed";
                    } else if (code == -1) {
                        statusStr = "error";
                    } else {
                        statusStr = "stopped";
                    }
                    LOG_INFO(std::string("[LobbyService] Wash car ") + statusStr + ", switching to idle mode");
                    setAIDetectionIdleMode(operation.getCameraId());
                }
            } catch (...) {
                // 解析失败，忽略
            }
        }
    );
}

void LobbyService::stopWashCarStatusMonitor() {
    m_washcarService.stopWashCarStatusMonitor();
}

void LobbyService::startDailyReportMonitor() {
    const int intervalMs = ConfigManager::instance().timing().dailyReportCheckIntervalMs;
    m_Timer.scheduleRepeated(intervalMs, [this]() {
        onDailyReportTimer();
    });
    LOG_INFO("[LobbyService] startDailyReportMonitor, intervalMs=" + std::to_string(intervalMs));
}

void LobbyService::stopDailyReportMonitor() {
    m_Timer.stopUpload();
}

void LobbyService::onDailyReportTimer() {
    m_washcarService.executeDailyReportTask(
        [this](const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendWashCarReportToMqtt(topicSuffix, payload, type);
        }
    );
}

void LobbyService::startValueChangeMonitor() {
    const int intervalMs = ConfigManager::instance().timing().valueChangeIntervalMs;
    m_Timer.scheduleRepeated(intervalMs, [this]() {
        onValueChangeTimer();
    });
    LOG_INFO("[LobbyService] startValueChangeMonitor, intervalMs=" + std::to_string(intervalMs));
}

void LobbyService::stopValueChangeMonitor() {
    m_Timer.stopUpload();
}

void LobbyService::onValueChangeTimer() {
    m_washcarService.executeValueChangeTask(
        [this](const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendWashCarReportToMqtt(topicSuffix, payload, type);
        }
    );
}

void LobbyService::startFaultMonitor() {
    const int intervalMs = ConfigManager::instance().timing().faultReportIntervalMs;
    m_Timer.scheduleRepeated(intervalMs, [this]() {
        onFaultTimer();
    });
    LOG_INFO("[LobbyService] startFaultMonitor, intervalMs=" + std::to_string(intervalMs));
}

void LobbyService::stopFaultMonitor() {
    m_Timer.stopUpload();
}

void LobbyService::onFaultTimer() {
    LOG_DEBUG("[LobbyService] fault monitor tick");
    m_washcarService.executeFaultReportTask(
        [this](const std::string& topicSuffix, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendWashCarReportToMqtt(topicSuffix, payload, type);
        }
    );
}

//点位操作-HTTP来源
LobbyResult<WashCarOperationResult> LobbyService::pointOnHttp(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointOnHttp" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointOnHttp")
        + ", reqSource=http"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("http");
    
    WashCarOperationResult result = m_washcarService.pointOn(op);
    Command cmd = Command::createPointOn(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);
    
    std::cout << "[LobbyService] finished: pointOnHttp" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointOffHttp(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointOffHttp" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointOffHttp")
        + ", reqSource=http"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("http");
    
    WashCarOperationResult result = m_washcarService.pointOff(op);
    Command cmd = Command::createPointOff(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);
    
    std::cout << "[LobbyService] finished: pointOffHttp" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointResetHttp(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointResetHttp" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointResetHttp")
        + ", reqSource=http"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("http");
    
    WashCarOperationResult result = m_washcarService.pointReset(op);
    Command cmd = Command::createPointReset(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);    
    std::cout << "[LobbyService] finished: pointResetHttp" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointStatusHttp(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointStatusHttp" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointStatusHttp")
        + ", reqSource=http"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("http");
    
    WashCarOperationResult result = m_washcarService.pointStatus(op);
    Command cmd = Command::createPointStatus(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);    
    std::cout << "[LobbyService] finished: pointStatusHttp" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

//点位操作-MQTT来源
LobbyResult<WashCarOperationResult> LobbyService::pointOnMqtt(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointOnMqtt" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointOnMqtt")
        + ", reqSource=mqtt"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("mqtt");
    
    WashCarOperationResult result = m_washcarService.pointOn(op);
    Command cmd = Command::createPointOn(op);
    m_commandService.executeCommand(cmd);
    
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);    
    std::cout << "[LobbyService] finished: pointOnMqtt" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointOffMqtt(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointOffMqtt" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointOffMqtt")
        + ", reqSource=mqtt"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("mqtt");
    
    WashCarOperationResult result = m_washcarService.pointOff(op);
    Command cmd = Command::createPointOff(op);
    m_commandService.executeCommand(cmd);
    
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);
    
    std::cout << "[LobbyService] finished: pointOffMqtt" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointResetMqtt(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointResetMqtt" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointResetMqtt")
        + ", reqSource=mqtt"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("mqtt");
    
    WashCarOperationResult result = m_washcarService.pointReset(op);
    Command cmd = Command::createPointReset(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);
    
    std::cout << "[LobbyService] finished: pointResetMqtt" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::pointStatusMqtt(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: pointStatusMqtt" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=pointStatusMqtt")
        + ", reqSource=mqtt"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint()+ ", mode=" + operation.getMode());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("mqtt");
    
    WashCarOperationResult result = m_washcarService.pointStatus(op);
    Command cmd = Command::createPointStatus(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_POINT_RESULT);
    
    std::cout << "[LobbyService] finished: pointStatusMqtt" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

//清除故障
LobbyResult<WashCarOperationResult> LobbyService::clearAlarmHttp(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: clearAlarmHttp" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=clearAlarmHttp")
        + ", reqSource=http"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("http");
    
    WashCarOperationResult result = m_washcarService.clearAlarm(op);
    Command cmd = Command::createClearAlarm(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_ALARM_CLEAR_RESULT);
    
    std::cout << "[LobbyService] finished: clearAlarmHttp" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

LobbyResult<WashCarOperationResult> LobbyService::clearAlarmMqtt(const WashCarOperation& operation) {
    std::cout << "[LobbyService] execute: clearAlarmMqtt" << std::endl;
    
    LOG_INFO(std::string("[LobbyService] LOBBY_ENTER cmd=clearAlarmMqtt")
        + ", reqSource=mqtt"
        + ", boxNo=" + operation.getBoxNo()
        + ", point=" + operation.getPoint());

    auto auth = m_safetykService.authenticate();
    if (!auth) { LOG_WARNING("[LobbyService] auth denied"); return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED); }
    
    WashCarOperation op = operation;
    op.setReqSource("mqtt");
    
    WashCarOperationResult result = m_washcarService.clearAlarm(op);
    Command cmd = Command::createClearAlarm(op);
    m_commandService.executeCommand(cmd);
    sendWashCarResult(cmd, op, result, WASHCAR_POINT_POST_TOPIC, MessageBuffer::MessageType::TYPE_ALARM_CLEAR_RESULT);
    
    std::cout << "[LobbyService] finished: clearAlarmMqtt" << std::endl;
    return result.getCode() == ErrorCode::Code::SUCCESS
        ? LobbyResult<WashCarOperationResult>::Ok(result)
        : LobbyResult<WashCarOperationResult>::Error(result.getCode());
}

// 处理云端 flexem/{boxNo}/reboot: 先发 commandreply 成功响应, 再异步延迟执行 systemctl reboot.
void LobbyService::handleFlexemReboot(const std::string& boxNo) {
    LOG_INFO(std::string("[LobbyService] handleFlexemReboot, boxNo=") + boxNo);

    // 1. 同步拼装 success 响应, 走现有 washcar 上报通道(经 MqttService 内部 buffer + 补 flexem/boxNo 拼 topic).
    auto reply = buildFlexemCommandReply();
    m_commandService.sendWashCarReportToMqtt(FLEXEM_COMMAND_REPLY_TOPIC, reply.dump(), MessageBuffer::MessageType::TYPE_WASHCAR_RESULT);

    // 2. 异步延迟执行 systemctl reboot, 给 MQTT 响应留出发时间, 不阻塞 MQTT 回调.
    // 现场 sudoers 收敛为 ztl NOPASSWD /usr/bin/systemctl reboot, 严禁使用 sudo reboot / /sbin/reboot.
    std::thread([boxNo]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        LOG_WARNING(std::string("[LobbyService] executing system reboot, boxNo=") + boxNo);
        int rc = std::system("sudo -n /usr/bin/systemctl reboot");
        if (rc != 0) {
            LOG_ERROR(std::string("[LobbyService] systemctl reboot failed, rc=") + std::to_string(rc) + ", boxNo=" + boxNo);
        }
    }).detach();
}

// AI 检测(占位: 智能检测暂未接入真实业务, 只留接口, 接入时再填实现)
std::shared_ptr<DetectionResult> LobbyService::getLatestDetectionResult(const std::string& cameraId) {
    (void)cameraId;
    return nullptr;   // 接入时改从 DetectionResultCache::read(regionId) 读
}

void LobbyService::onPointValueChanged(const std::string& plcId, const std::string& pointId, bool value) {
    if (value) {
        LOG_INFO(std::string("[LobbyService] Point changed to TRUE: ") + plcId + ":" + pointId);
        // 接入时: 红外点信号 -> setAIDetectionWorkingMode(区域id)
    }
}

bool LobbyService::checkDetectionBeforeWashCar(const std::string& cameraId) {
    // [FIX-06] 保持空实现: "AI 插入洗车流程的具体语义"尚未拍板。
    //   下方死代码一旦启用,就会用最新检测结果拦截洗车并上报异常,
    //   在语义拍板前删除 return true 将直接改变洗车流程行为。如需启用,请先定语义。
    return true;
    auto result = getLatestDetectionResult(cameraId);
    if (!result || !result->isValid()) {
        return false;
    }

    if (result->hasItem(DetectionResult::Type::PERSON_DETECTED)) {
        // [B-NOT-YET-SENT] 云端还没有 AI 检测上报接口, 接口就绪后再打开
        // reportAIDetectionException(cameraId, "区域有人，禁止洗车", MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        return false;
    }

    if (result->hasItem(DetectionResult::Type::DOOR_OPEN)) {
        // [B-NOT-YET-SENT] 同上
        // reportAIDetectionException(cameraId, "车门打开，禁止洗车", MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        return false;
    }

    if (result->hasItem(DetectionResult::Type::WINDOW_OPEN)) {
        // [B-NOT-YET-SENT] 同上
        // reportAIDetectionException(cameraId, "车窗打开，禁止洗车", MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        return false;
    }

    if (result->hasItem(DetectionResult::Type::ROOF_OBSTACLE)) {
        // [B-NOT-YET-SENT] 同上
        // reportAIDetectionException(cameraId, "车顶异物，禁止洗车", MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        return false;
    }

    // TODO: 车牌记录检查 - 无记录或有异常不允许启动
    // 需要检查 result->hasItem(DetectionResult::Type::LICENSE_PLATE)
    // 如果没有车牌记录，应该禁止启动洗车

    return true;
}

void LobbyService::startAIDetectionMonitor() {
    // 定时驱动检测服务跑一轮(检测服务内部按区域模式判到期: 低频 6s / 高频 2s)
    const int intervalMs = ConfigManager::instance().ai().workingIntervalMs;
    m_Timer.scheduleRepeated(intervalMs, [this]() {
        updateAIDetectionFrequency();
    });
    LOG_INFO("[LobbyService] startAIDetectionMonitor, intervalMs=" + std::to_string(intervalMs));
}

void LobbyService::updateAIDetectionFrequency() {
    // 触发一轮检测(异步, 模型线程跑)
    m_detectionService.detectAllRegions();

    // 读结果取待报项(检测服务已按状态变化去重, 同一异常不会反复产生待报项)
    for (const auto& rb : ConfigManager::instance().ai().regions) {
        auto result = m_detectionService.getLatestDetectionResult(rb.regionId);
        if (!result) continue;
        for (const auto& alarm : result->alarms) {
            LOG_WARNING("[LobbyService][AI] 待报项 camera=" + rb.regionId
                        + " type=" + DetectionResult::itemTypeName(alarm.itemType)
                        + " image=" + alarm.imagePath);
            // [B-NOT-YET-SENT] 云端还没有 AI 检测上报接口, 接口就绪后打开:
            // reportAIDetectionException(rb.regionId, "AI 检测异常", MessageBuffer::MessageType::TYPE_FAULT_REPORT);
        }
        m_detectionService.consumeAlarms(rb.regionId);
    }
}

void LobbyService::setAIDetectionIdleMode(const std::string& cameraId) {
    m_detectionService.decreaseFrequency(cameraId);
}

void LobbyService::setAIDetectionWorkingMode(const std::string& cameraId) {
    m_detectionService.increaseFrequency(cameraId);
}

void LobbyService::reportAIDetectionException(const std::string& cameraId, const std::string& message, MessageBuffer::MessageType msgType) {
    // 云端上报走洗车事件通道(故障/报警)
    json msg;
    msg["cameraId"] = cameraId;
    msg["message"] = message;
    msg["time"] = getCurrentTimeSeconds();
    m_commandService.sendWashCarReportToMqtt(WASHCAR_EVENT_POST_TOPIC, msg.dump(), msgType);
}
