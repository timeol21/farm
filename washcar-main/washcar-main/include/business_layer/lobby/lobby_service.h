#pragma once 
class ICommandService;

#include "business_layer/timer.h"
#include "business_layer/safety/safety_service.h"
#include "business_layer/command/command_service.h"
#include "business_layer/device/device_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/detection/detection.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "business_layer/lobby/lobby_object.h"
#include <atomic>
#include <cstdio>
#include <memory>

// 大厅 = 业务流程的生命周期与编排。
// 云端报文的解析/组包/主题全部在命令服务, 这里只做"收到业务请求 -> 校验 -> 调服务 -> 交回结果"。
class ILobbyService {
public:
    // ================= 查询 =================
    // PLC 软元件快照: 洗车服务是设备状态缓冲的唯一读者, 这里只做转发
    virtual std::vector<FxPlcStatus> viewPlcStatus() = 0;

    virtual LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery& query) = 0;//查看当前的视频帧                
    virtual LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) = 0;////查看当前的历史视频时间段
    virtual LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery& query) = 0;//查看报警记录

    // 配置台用:洗车中禁止应用会重启的配置;MQTT 在线状态用于概览
    virtual bool isWashCarWashing() = 0;
    virtual bool isMqttConnected() = 0;

    // ================= 下载 =================
    virtual LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download) = 0;//下载历史视频

    // ================= 控制 =================
    // virtual LobbyResult operateDoorLock(const DoorLockOperation& operation) = 0;//控制门锁
    virtual LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation& operation) = 0;//控制电磁阀

    //洗车业务(云端命令与本地 HTTP 共用同一套入口)
    virtual LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation& operation) = 0;

    // 驿公里独有命令
    virtual LobbyResult<WashCarOperationResult> abortWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopWork(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopService(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> resetSafe(const WashCarOperation& operation) = 0;

    virtual LobbyResult<bool> changeMode(const WashCarOperation& operation) = 0;

    //点位操作-HTTP来源(云端点位命令已随旧协议一起下线)
    virtual LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation& operation) = 0;

    //清除故障(本地 HTTP)
    virtual LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation& operation) = 0;

	// AI 检测
	virtual std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) = 0;
	virtual bool checkDetectionBeforeWashCar(const std::string& cameraId) = 0;
	virtual void onPointValueChanged(const std::string& plcId, const std::string& pointId, bool value) = 0;// plc车在点位变化

    virtual ~ILobbyService() = default;
};



class LobbyService : public ILobbyService{
public:
    LobbyService(ISafetyService& safetyService, ICommandService& commandService,
                 IDeviceService& deviceService, IWashCarService& washcarService,
                 ITimer& timer, IDetectionService& detectionService);
    ~LobbyService() = default;  

   // 生命周期: 大厅按顺序启动/停止所有服务的定时任务(线程归 Timer)
    void start();
    void stop();

   // ================= 查询 =================
    std::vector<FxPlcStatus> viewPlcStatus() override;
    LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery& query) override;
    LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) override;
    LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery& query) override;

    // ================= 下载 =================
    LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download) override;

    // ================= 控制 =================
    LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation& operation) override;

    bool isWashCarWashing() override { return m_washcarService.isWashing(); }
    bool isMqttConnected() override { return m_commandService.isMqttConnected(); }

    //洗车业务
    LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation& operation) override;

    // 驿公里独有命令(中转实现)
    LobbyResult<WashCarOperationResult> abortWashCar(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> stopWork(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> stopService(const WashCarOperation& operation) override;
    LobbyResult<WashCarOperationResult> resetSafe(const WashCarOperation& operation) override;

    LobbyResult<bool> changeMode(const WashCarOperation& operation) override;
	
	//点位操作-HTTP来源
	LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation& operation) override;
	
	//清除故障
	LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation& operation) override;

	// AI 检测
	std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) override;
	bool checkDetectionBeforeWashCar(const std::string& cameraId) override;
	void onPointValueChanged(const std::string& plcId, const std::string& pointId, bool value) override;
	void updateAIDetectionFrequency(); // 公共: 大厅定时器调用, 驱动 AI 检测一轮(升降频+报警+派发)

private:
    // 监控洗车状态
    void startWashCarStatusMonitor(const WashCarOperation& operation);
    void stopWashCarStatusMonitor();

    // 定时任务体(注册见 start())
    void onDailyReportTimer();
    void onValueChangeTimer();
    void onFaultTimer();

    // AI 检测: 大厅定时器驱动, 业务判定在检测服务内部
    void setAIDetectionIdleMode(const std::string& cameraId);
    void setAIDetectionWorkingMode(const std::string& cameraId);
    void reportAIDetectionException(const std::string& cameraId, const std::string& message, MessageBuffer::MessageType msgType);

private:
    IDeviceService& m_deviceService; // 设备

    ISafetyService& m_safetykService; // 安全
     
    ICommandService& m_commandService; //命令

    IWashCarService& m_washcarService; // 洗车

    IDetectionService& m_detectionService; // ai检测

    ITimer& m_Timer; //定时器
};
