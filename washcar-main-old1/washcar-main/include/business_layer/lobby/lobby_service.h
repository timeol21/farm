#pragma once 
class ICommandService;

#include "common/topics.h"
#include "business_layer/timer.h"
#include "business_layer/safety/safety_service.h"
#include "business_layer/command/command_service.h"
#include "business_layer/command/command_object.h"
#include "business_layer/device/device_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/detection/detection.h"
#include "business_layer/command/mqtt/message_buffer.h"
#include "business_layer/lobby/lobby_object.h"
#include <atomic>
#include <cstdio>
#include <memory>

class ILobbyService {
public:
    // ================= 查询 =================
    virtual LobbyResult<BoxDeviceStatus> retrieveDeviceStatus(const DeviceStatusQuery& query) = 0;
    // virtual LobbyResult retrieveSensorStatus(const SensorQuery& query) = 0;
    // virtual LobbyResult retrieveEnvironmentData(const EnvironmentQuery& query) = 0;
    
    virtual LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery& query) = 0;//查看当前的视频帧                
    virtual LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) = 0;////查看当前的历史视频时间段
    virtual LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery& query) = 0;//查看报警记录

    // ================= 下载 =================
    virtual LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download) = 0;//下载历史视频

    // ================= 控制 =================
    // virtual LobbyResult operateDoorLock(const DoorLockOperation& operation) = 0;//控制门锁
    virtual LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation& operation) = 0;//控制电磁阀

   
    virtual LobbyResult<void> startDeviceStatusUpload(const DeviceStatusQuery& query) = 0; //定时上传(设备状态)
    // virtual LobbyResult controlTrolleyRotation(const TrolleyOperation& operation) = 0;//控制小车旋转
    // virtual LobbyResult controlRotateCamera(const CameraOperation& operation) = 0;//控制旋转摄像头

 //洗车业务
    virtual LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> getWashCarStatus(const WashCarOperation& operation) = 0;

    // 驿公里独有命令
    virtual LobbyResult<WashCarOperationResult> abortWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopWork(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopService(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> resetSafe(const WashCarOperation& operation) = 0;

    virtual LobbyResult<WashCarOperationResult> clearAlarm(const WashCarOperation& operation) = 0;
    virtual LobbyResult<bool> changeMode(const WashCarOperation& operation) = 0;

    //点位操作-HTTP来源
    virtual LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation& operation) = 0;

    //点位操作-MQTT来源
    virtual LobbyResult<WashCarOperationResult> pointOnMqtt(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointOffMqtt(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointResetMqtt(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> pointStatusMqtt(const WashCarOperation& operation) = 0;

    //清除故障
    virtual LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> clearAlarmMqtt(const WashCarOperation& operation) = 0;

    // ================= 盒子级 Flexem 命令 =================
    // 处理云端 flexem/{boxNo}/reboot 指令: 先发 commandreply 成功响应, 再延迟执行 systemctl reboot。
    virtual void handleFlexemReboot(const std::string& boxNo) = 0;

    // // ================= 配置 =================
    // virtual LobbyResult configureCamera(const CameraConfiguration& config) = 0;
    // virtual LobbyResult updateBoxConfiguration(const BoxConfiguration& configuration) = 0;

    // ================= AI 模型 =================
    // virtual LobbyResult deployAIModel(const AIModelDeploy& deploy) = 0;
    // virtual LobbyResult enableAIModel(const AIModelEnable& enable) = 0;
    // virtual LobbyResult disableAIModel(const AIModelDisable& disable) = 0;
    // virtual LobbyResult updateAIModel(const AIModelUpdate& update) = 0;

	// AI 检测
	virtual std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) = 0;
	virtual bool checkDetectionBeforeWashCar(const std::string& cameraId) = 0;
	virtual void onPointValueChanged(const std::string& plcId, const std::string& pointId, bool value) = 0;// plc车在点位变化

    virtual ~ILobbyService() = default;
};



class LobbyService : public ILobbyService{
public:
    LobbyService(ISafetyService& safetyService, ICommandService& commandService, IDeviceService& deviceService, IWashCarService& washcarService, ITimer& timer, DeviceStatusCache& deviceStatusCache, IDetectionService& detectionService);
    ~LobbyService() = default;  

   // ================= 查询 =================


    LobbyResult<BoxDeviceStatus> retrieveDeviceStatus(const DeviceStatusQuery& query) override; //2.
    // LobbyResult retrieveSensorStatus(const SensorQuery& query) override;
    // LobbyResult retrieveEnvironmentData(const EnvironmentQuery& query) override;
    LobbyResult<FrameQuery> retrieveLiveCameraFrame(const FrameQuery& query) override;
    LobbyResult<FrameQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) override;
    LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery& query) override;

    // ================= 下载 =================
    LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download) override; //3.

    // ================= 控制 =================
    // LobbyResult operateDoorLock(const DoorLockOperation& operation) override;

    LobbyResult<void> startDeviceStatusUpload(const DeviceStatusQuery& query)override; //1.

     //fx三菱plc
    LobbyResult<SolenoidValveOperationResult> operateFxSolenoidValve(const std::string& plcId, int yOctal, const std::string& cmd);

    LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation& operation) override;
    // LobbyResult controlTrolleyRotation(const TrolleyOperation& operation) override;
    // LobbyResult controlRotateCamera(const CameraOperation& operation) override;

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

    LobbyResult<WashCarOperationResult> clearAlarm(const WashCarOperation& operation) override;
    LobbyResult<bool> changeMode(const WashCarOperation& operation) override;
	
	//点位操作-HTTP来源
	LobbyResult<WashCarOperationResult> pointOnHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointOffHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointResetHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointStatusHttp(const WashCarOperation& operation) override;
	
	//点位操作-MQTT来源
	LobbyResult<WashCarOperationResult> pointOnMqtt(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointOffMqtt(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointResetMqtt(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> pointStatusMqtt(const WashCarOperation& operation) override;
	
	//清除故障
	LobbyResult<WashCarOperationResult> clearAlarmHttp(const WashCarOperation& operation) override;
	LobbyResult<WashCarOperationResult> clearAlarmMqtt(const WashCarOperation& operation) override;

	// 盒子级 Flexem 命令 (reboot) : 异步发 commandreply 响应 + 延迟执行 systemctl reboot
	void handleFlexemReboot(const std::string& boxNo) override;

	// AI 检测
	std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) override;
	bool checkDetectionBeforeWashCar(const std::string& cameraId) override;
	void onPointValueChanged(const std::string& plcId, const std::string& pointId, bool value) override;
	void updateAIDetectionFrequency(); // 公共: 大厅定时器调用, 驱动 AI 检测一轮(升降频+报警+派发)

private:
    // ================= 配置 =================
    // LobbyResult configureCamera(const CameraConfiguration& config) override;
    // LobbyResult updateBoxConfiguration(const BoxConfiguration& configuration) override;

    // ================= AI 模型 =================
    // LobbyResult deployAIModel(const AIModelDeploy& deploy) override;
    // LobbyResult enableAIModel(const AIModelEnable& enable) override;
    // LobbyResult disableAIModel(const AIModelDisable& disable) override;
    // LobbyResult updateAIModel(const AIModelUpdate& update) override;

private:
    void TimingProcessing(); 

    bool TimingUpload();

    void TimingPullVideoFrame();

    void sendSolenoidResult(const Command& cmd, const SolenoidValveOperation& op ,const DeviceOperationResult& result);

    void sendBoxStatusResult(const BoxDeviceStatus& boxStatus);

    void sendWashCarResult(const Command& cmd, const WashCarOperation& op, const WashCarOperationResult& result, const std::string& topicSuffix, MessageBuffer::MessageType msgType);

    // 监控洗车状态
    void startWashCarStatusMonitor(const WashCarOperation& operation);
    void stopWashCarStatusMonitor();

    // 每天8点定时上报 m257/m259（异常首次上报）
    void startDailyReportMonitor();
    void stopDailyReportMonitor();
    void onDailyReportTimer();

    // 变化上报
    void startValueChangeMonitor();
    void stopValueChangeMonitor();
    void onValueChangeTimer();

    // 故障上报
    void startFaultMonitor();
    void stopFaultMonitor();
    void onFaultTimer();

    // AI 检测: 大厅定时器驱动(参考洗车变化上报), 业务判定在检测服务内部
    void startAIDetectionMonitor();
    void setAIDetectionIdleMode(const std::string& cameraId);
    void setAIDetectionWorkingMode(const std::string& cameraId);
    void reportAIDetectionException(const std::string& cameraId, const std::string& message, MessageBuffer::MessageType msgType);

private:

    IDeviceService& m_deviceService; // 设备

    ISafetyService& m_safetykService; // 安全
     
    ICommandService& m_commandService; //命令

    IWashCarService& m_washcarService; // 洗车

    IDetectionService& m_detectionService; // ai检测

    // FrameBuffer& m_FrameBuffer;         //帧缓冲对象

    // EquipmentStatusBuffer& m_equipmentBuffer;//设备状态缓冲对象

    DeviceStatusCache& deviceStatusCache_;//设备状态缓存
    ITimer& m_Timer; //定时器
};
