#include "business_layer/lobby/lobby_service.h"
#include "business_layer/lobby/lobby_object.h"
#include "common/log/log_manager.h"
#include <string>
#include <sstream>
//public
 LobbyService::LobbyService(ISafetyService& safetyService, ICommandService& commandService,IDeviceService& deviceService, IWashCarSercvice& washcarServiece,ITimer & timer,DeviceStatusCache& deviceStatusCache )
    :  m_safetykService(safetyService), m_commandService(commandService),m_deviceService(deviceService),m_washcarServiece(washcarServiece), m_Timer(timer),deviceStatusCache_(deviceStatusCache)
{
    
}


LobbyResult<BoxDeviceStatus> LobbyService::retrieveDeviceStatus(const DeviceStatusQuery& query){
    auto auth = m_safetykService.authenticate();
    if (!auth)return LobbyResult<BoxDeviceStatus>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);   
    if(query.getReqSource() == "http"){
        //去拿设备状态  
        BoxDeviceStatus status = m_deviceService.viewAllDeviceStatus();
    } else{
      
    }
    return LobbyResult<BoxDeviceStatus>::Ok(BoxDeviceStatus());
    
    
}


LobbyResult<SensorQuery> LobbyService::retrieveLiveCameraFrame(const FrameQuery& query)
{
    auto auth = m_safetykService.authenticate();
    if (!auth)return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
    // 获取视频帧的逻辑 去帧缓冲里面去拿
    //1.正确
    return LobbyResult<SensorQuery>::Ok(SensorQuery());
    //2.错误
    // return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    //3.异常错误
}

LobbyResult<SensorQuery> LobbyService::retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query)
{
    auto auth = m_safetykService.authenticate();
    if (!auth)return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
    // 获取历史视频时间段的逻辑 
    //1.正确
    return LobbyResult<SensorQuery>::Ok(SensorQuery());
    //2.错误
    // return LobbyResult<SensorQuery>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    //3.异常错误
}

LobbyResult<SensorQuery> LobbyService::retrieveAlarmRecords(const AlarmQuery& query)
{
    auto auth = m_safetykService.authenticate();
    if (!auth)return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
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
    if (!auth)return LobbyResult<SensorQuery>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
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
    if (!auth) return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

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
    if (!auth) return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

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
    if (!auth)return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

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

LobbyResult<WashCarOperationResult> LobbyService::startWashCar(const WashCarOperation& operation)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

    if (!operation.isValid()) {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    WashCarOperationResult result = m_washcarServiece.startWashCar(operation);

    if (result.getCode() == 0) {
        startWashCarStatusMonitor(operation);
    }

    Command cmd = Command::createWashCarStart(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        sendWashCarResult(cmd, operation, result);
    }

    return LobbyResult<WashCarOperationResult>::Ok(result);
}

LobbyResult<WashCarOperationResult> LobbyService::stopWashCar(const WashCarOperation& operation)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

    if (!operation.isValid()) {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    WashCarOperationResult result = m_washcarServiece.stopWashCar(operation);

    if (result.getCode() == -2) {
        stopWashCarStatusMonitor();
    }

    Command cmd = Command::createWashCarStop(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        sendWashCarResult(cmd, operation, result);
    }

    return LobbyResult<WashCarOperationResult>::Ok(result);
}

LobbyResult<WashCarOperationResult> LobbyService::resetWashCar(const WashCarOperation& operation)
{
    auto auth = m_safetykService.authenticate();
    if (!auth) return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);

    if (!operation.isValid()) {
        return LobbyResult<WashCarOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }

    WashCarOperationResult result = m_washcarServiece.resetWashCar(operation);

    Command cmd = Command::createWashCarReset(operation);
    m_commandService.executeCommand(cmd);

    if (operation.getReqSource() == "mqtt") {
        sendWashCarResult(cmd, operation, result);
    }

    return LobbyResult<WashCarOperationResult>::Ok(result);
}

LobbyResult<void> LobbyService::startDeviceStatusUpload(const DeviceStatusQuery& query){
    auto auth = m_safetykService.authenticate();
    if (!auth)return LobbyResult<void>::Error(ErrorCode::Code::AUTH_PERMISSION_DENIED);
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
    // msg["sensor"] = boxStatus.getSensorStatusList();
    // msg["camera"] = boxStatus.getCameraStatusList();
    // msg["infrared"] = boxStatus.getInfraredSensorStatusList();
    // msg["smokeDetector"] = boxStatus.getSmokeDetectorStatusList();
    // msg["waterLevel"] = boxStatus.getWaterLevelSensorStatusList();
    // msg["doorLock"] = boxStatus.getDoorLockStatusList();

    // MQTT 发送
    std::string topic = "device/uploadStatus/result";
    m_commandService.sendCommandResultToMqtt(topic, msg.dump());
}


void LobbyService::sendSolenoidResult(const Command& cmd, const SolenoidValveOperation& op,const DeviceOperationResult& result){
    if(op.getReqSource() == "http") return;
    json msg;
    msg["cmdId"] = cmd.getCmdId();
    msg["deviceId"] = cmd.getDeviceId();
    msg["plcId"] = op.getPlcId();
    msg["success"] = result.operationBool()? "0" : "-1";

    std::string topic = "device/solenoid/result";

    m_commandService.sendCommandResultToMqtt(topic, msg.dump());
}

void LobbyService::sendWashCarResult(const Command& cmd, const WashCarOperation& op, const WashCarOperationResult& result) {
    json msg;
    msg["boxNo"] = result.getBoxNo();
    msg["time"] = op.getTime();
    msg["message"] = result.getMessage();

    char topic[128];
    snprintf(topic, sizeof(topic), "carwash/%s/command", op.getBoxNo().c_str());

    m_commandService.sendCommandResultToMqtt(topic, msg.dump());
}

void LobbyService::startWashCarStatusMonitor(const WashCarOperation& operation) {
    if (m_Timer.isRunningUpload()) return;

    int interval = 500;

    m_Timer.scheduleRepeated(interval, [this, operation]() {
        onWashCarStatusTimer(operation);
    });
}

void LobbyService::stopWashCarStatusMonitor() {
    m_Timer.stopUpload();
}

void LobbyService::onWashCarStatusTimer(const WashCarOperation& operation) {
    auto status = m_washcarServiece.getWashCarStatus(operation);

    if (!m_washcarServiece.isWashing()) {
        stopWashCarStatusMonitor();
        return;
    }

    if (operation.getReqSource() == "mqtt") {
        json msg;
        msg["d142"] = status.getCurrentStep();
        msg["boxNo"] = status.getBoxNo();

        char topic[128];
        snprintf(topic, sizeof(topic), "carwash/%s/monitor", operation.getBoxNo().c_str());

        //上传开始洗车消息
        m_commandService.sendCommandResultToMqtt(topic, msg.dump());
    }

    if (status.isCompleted() || status.hasFault()) {
        stopWashCarStatusMonitor();

        if (operation.getReqSource() == "mqtt") {
            Command cmd = Command::createWashCarStart(operation);
            cmd.setCmdState(status.hasFault() ? CommandState::STATE_FAILED : CommandState::STATE_SUCCESS);
            m_commandService.updateCommandState(cmd.getCmdId(), cmd.getCmdState());

            // 洗车完成或发生错误都上传
            sendWashCarResult(cmd, operation, status);
        }
    }
}
