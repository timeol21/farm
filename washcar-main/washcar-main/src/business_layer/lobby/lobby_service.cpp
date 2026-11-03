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

static std::int64_t getCurrentTimeSeconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

// 业务结果 -> 大厅结果。成功只有一个码(SUCCESS); 失败把业务文案透传给云端回执。
static LobbyResult<WashCarOperationResult> toLobbyResult(const WashCarOperationResult& result) {
    if (result.getCode() == ErrorCode::Code::SUCCESS) {
        return LobbyResult<WashCarOperationResult>::Ok(result);
    }
    return LobbyResult<WashCarOperationResult>::Error(result.getCode(), result.getMessage());
}

//public
LobbyService::LobbyService(ISafetyService& safetyService, ICommandService& commandService,
                           IDeviceService& deviceService, IWashCarService& washcarService,
                           ITimer& timer, IDetectionService& detectionService)
    : m_safetykService(safetyService), m_commandService(commandService),
      m_deviceService(deviceService), m_washcarService(washcarService),
      m_Timer(timer), m_detectionService(detectionService)
{
}

// 所有周期的任务都在这里注册, 线程归 Timer; 顺序 = 采集先起来, 再是上报/检测判定
void LobbyService::start() {
    const auto& timing = ConfigManager::instance().timing();

    // 采集是阻塞 IO(串口一轮几百毫秒), 用 worker 任务, 不占用调度线程
    m_Timer.addTask("device_acquisition", timing.acquisitionIntervalMs, TimerTaskMode::WORKER,
                    [this]() { m_deviceService.acquisitionOnce(); });

    m_Timer.addTask("daily_report", timing.dailyReportCheckIntervalMs, TimerTaskMode::INLINE,
                    [this]() { onDailyReportTimer(); });
    m_Timer.addTask("value_change", timing.valueChangeIntervalMs, TimerTaskMode::INLINE,
                    [this]() { onValueChangeTimer(); });
    m_Timer.addTask("fault_report", timing.faultReportIntervalMs, TimerTaskMode::INLINE,
                    [this]() { onFaultTimer(); });
    // 检测是同步推理(NPU 串行), 放 worker 任务, 不占调度线程
    m_Timer.addTask("ai_detection", ConfigManager::instance().ai().workingIntervalMs,
                    TimerTaskMode::WORKER, [this]() { updateAIDetectionFrequency(); });

    m_Timer.start();
    LOG_INFO("[LobbyService] start: tasks=" + std::to_string(m_Timer.taskCount()));
}

void LobbyService::stop() {
    m_Timer.stop();
    m_washcarService.stopWashCarStatusMonitor();
    LOG_INFO("[LobbyService] stop");
}

std::vector<FxPlcStatus> LobbyService::viewPlcStatus() {
    return m_washcarService.viewPlcStatus();
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

        // 记录命令(结果上报由命令服务按端点决定, 大厅不再拼报文)
        DeviceOperationResult result(success ? 0 : -1, success ? "Success" : "Fail");
        m_commandService.recordCommand(operation.getCmd() == "open" ? Command::Type::SOLENOID_OPEN
                                                                   : Command::Type::SOLENOID_CLOSE,
                                       operation.getDeviceId());

        SolenoidValveOperationResult opResult = SolenoidValveOperationResult::createResult(result, operation);
        return LobbyResult<SolenoidValveOperationResult>::Ok(opResult);
    } catch (const std::exception& e) {
        LOG_WARNING("操作电磁阀出错 " + std::string("Exception: ") + e.what());
        return LobbyResult<SolenoidValveOperationResult>::Error(ErrorCode::Code::SERVER_INTERNAL_ERROR);
    }
}




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

    m_commandService.recordCommand(Command::Type::WASHCAR_START, operation.getBoxNo());

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=startWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - startEnterMs));
    std::cout << "[LobbyService] finished: startWashCar" << std::endl;

    return toLobbyResult(result);
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
        // 停洗成功: 停掉进度监控, 否则停了还在按周期轮询 PLC 往云端发进度
        stopWashCarStatusMonitor();
    }

    m_commandService.recordCommand(Command::Type::WASHCAR_STOP, operation.getBoxNo());

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopEnterMs));
    std::cout << "[LobbyService] finished: stopWashCar" << std::endl;

    return toLobbyResult(result);
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

    m_commandService.recordCommand(Command::Type::WASHCAR_RESET, operation.getBoxNo());

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=resetWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - resetEnterMs));
    std::cout << "[LobbyService] finished: resetWashCar" << std::endl;

    return toLobbyResult(result);
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

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=abortWashCar")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - abortEnterMs));
    std::cout << "[LobbyService] finished: abortWashCar" << std::endl;

    return toLobbyResult(result);
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

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopWork")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopWorkEnterMs));
    std::cout << "[LobbyService] finished: stopWork" << std::endl;

    return toLobbyResult(result);
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

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=stopService")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - stopServiceEnterMs));
    std::cout << "[LobbyService] finished: stopService" << std::endl;

    return toLobbyResult(result);
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

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=resetSafe")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - resetSafeEnterMs));
    std::cout << "[LobbyService] finished: resetSafe" << std::endl;

    return toLobbyResult(result);
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

    LOG_INFO(std::string("[LobbyService] LOBBY_EXIT cmd=getWashCarStatus")
        + ", boxNo=" + operation.getBoxNo()
        + ", result=" + std::to_string(static_cast<int>(result.getCode()))
        + ", message=" + result.getMessage()
        + ", cmdId=" + trace::cmdId
        + ", totalMs=" + std::to_string(trace::nowMs() - readEnterMs));
    std::cout << "[LobbyService] finished: getWashCarStatus" << std::endl;

    return toLobbyResult(result);
}

LobbyResult<bool> LobbyService::changeMode(const WashCarOperation& operation)
{
    std::cout << "[LobbyService] execute: changeMode" << std::endl;

    WashCarOperationResult result = m_washcarService.maintain(operation);
    if (result.getCode() == ErrorCode::Code::SUCCESS) {
        std::cout << "[LobbyService] finished: changeMode" << std::endl;
        return LobbyResult<bool>::Ok(true);
    }

    m_commandService.recordCommand(Command::Type::CHANGE_MODE, operation.getBoxNo());

    std::cout << "[LobbyService] finished: changeMode" << std::endl;

    return LobbyResult<bool>::Error(result.getCode(), result.getMessage());
}

void LobbyService::startWashCarStatusMonitor(const WashCarOperation& operation) {
    m_washcarService.startWashCarStatusMonitor(
        operation,
        [this, &operation](Command::Endpoint endpoint, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendReport(endpoint, payload, type);

            // 检测洗车完成，自动恢复空闲模式
            try {
                auto j = nlohmann::json::parse(payload);
                int code = j.value("code", 0);
                // 进度报文里的点位键是大写("D142",由点位表的 cloudKey 决定),
                // json 查键区分大小写 —— 写成小写恒得 0,洗完车的判定永远不成立,
                // AI 一直停在工作模式,检测服务持续吃满 CPU/NPU。
                int currentStep = j.value("D142", 0);

                // code == 0 && currentStep == 10 → 完成
                // code != 0 → 错误
                if ((code == 0 && currentStep == 10) || code == -1) {
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

void LobbyService::onDailyReportTimer() {
    m_washcarService.executeDailyReportTask(
        [this](Command::Endpoint endpoint, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendReport(endpoint, payload, type);
        }
    );
}

void LobbyService::onValueChangeTimer() {
    m_washcarService.executeValueChangeTask(
        [this](Command::Endpoint endpoint, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendReport(endpoint, payload, type);
        }
    );
}

void LobbyService::onFaultTimer() {
    LOG_DEBUG("[LobbyService] fault monitor tick");
    m_washcarService.executeFaultReportTask(
        [this](Command::Endpoint endpoint, const std::string& payload, MessageBuffer::MessageType type) {
            m_commandService.sendReport(endpoint, payload, type);
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
    m_commandService.recordCommand(Command::Type::POINT_ON, op.getPoint());
    
    std::cout << "[LobbyService] finished: pointOnHttp" << std::endl;
    return toLobbyResult(result);
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
    m_commandService.recordCommand(Command::Type::POINT_OFF, op.getPoint());
    
    std::cout << "[LobbyService] finished: pointOffHttp" << std::endl;
    return toLobbyResult(result);
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
    m_commandService.recordCommand(Command::Type::POINT_RESET, op.getPoint());    
    std::cout << "[LobbyService] finished: pointResetHttp" << std::endl;
    return toLobbyResult(result);
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
    m_commandService.recordCommand(Command::Type::POINT_STATUS, op.getPoint());    
    std::cout << "[LobbyService] finished: pointStatusHttp" << std::endl;
    return toLobbyResult(result);
}

//清除故障(本地 HTTP)
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
    m_commandService.recordCommand(Command::Type::CLEAR_ALARM, op.getPoint());

    std::cout << "[LobbyService] finished: clearAlarmHttp" << std::endl;
    return toLobbyResult(result);
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
    m_commandService.sendReport(Command::Endpoint::PUSHALARM, msg.dump(), msgType);
}
