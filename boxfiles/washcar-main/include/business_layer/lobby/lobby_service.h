#pragma once 
class ICommandService;

#include "business_layer/timer.h"
#include "business_layer/safety/safety_service.h"
#include "business_layer/command/command_service.h"
#include "business_layer/command/command_object.h"
#include "business_layer/device/device_service.h"
#include "business_layer/washcar/washcar_service.h"
#include "business_layer/lobby/lobby_object.h"
#include <atomic>
#include <cstdio>

class ILobbyService {
public:
    // ================= 查询 =================
    virtual LobbyResult<BoxDeviceStatus> retrieveDeviceStatus(const DeviceStatusQuery& query) = 0;
    // virtual LobbyResult retrieveSensorStatus(const SensorQuery& query) = 0;
    // virtual LobbyResult retrieveEnvironmentData(const EnvironmentQuery& query) = 0;
    
    virtual LobbyResult<SensorQuery> retrieveLiveCameraFrame(const FrameQuery& query) = 0;//查看当前的视频帧                
    virtual LobbyResult<SensorQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) = 0;////查看当前的历史视频时间段
    virtual LobbyResult<SensorQuery> retrieveAlarmRecords(const AlarmQuery& query) = 0;//查看报警记录

    // ================= 下载 =================
    virtual LobbyResult<SensorQuery> downloadHistoricalCameraFootage(const DownloadHistoricalVideo& download) = 0;//下载历史视频

    // ================= 控制 =================
    // virtual LobbyResult operateDoorLock(const DoorLockOperation& operation) = 0;//控制门锁
    virtual LobbyResult<SolenoidValveOperationResult> operateSolenoidValve(const SolenoidValveOperation& operation) = 0;//控制电磁阀

   
    virtual LobbyResult<void> startDeviceStatusUpload(const DeviceStatusQuery& query) = 0; //定时上传(设备状态)
    // virtual LobbyResult controlTrolleyRotation(const TrolleyOperation& operation) = 0;//控制小车旋转
    // virtual LobbyResult controlRotateCamera(const CameraOperation& operation) = 0;//控制旋转摄像头

    virtual LobbyResult<WashCarOperationResult> startWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> stopWashCar(const WashCarOperation& operation) = 0;
    virtual LobbyResult<WashCarOperationResult> resetWashCar(const WashCarOperation& operation) = 0;

    // // ================= 配置 =================
    // virtual LobbyResult configureCamera(const CameraConfiguration& config) = 0;
    // virtual LobbyResult updateBoxConfiguration(const BoxConfiguration& configuration) = 0;

    // ================= AI 模型 =================
    // virtual LobbyResult deployAIModel(const AIModelDeploy& deploy) = 0;
    // virtual LobbyResult enableAIModel(const AIModelEnable& enable) = 0;
    // virtual LobbyResult disableAIModel(const AIModelDisable& disable) = 0;
    // virtual LobbyResult updateAIModel(const AIModelUpdate& update) = 0;

    virtual ~ILobbyService() = default;
};



class LobbyService : public ILobbyService{
public:
    LobbyService(ISafetyService& safetyService, ICommandService& commandService, IDeviceService&  deviceService, IWashCarSercvice& washcarServiece, ITimer& timer, DeviceStatusCache& deviceStatusCache);/*  IDeviceService& deviceService,IDetectionService& detectionService */
    ~LobbyService() = default;  

   // ================= 查询 =================


    LobbyResult<BoxDeviceStatus> retrieveDeviceStatus(const DeviceStatusQuery& query) override; //2.
    // LobbyResult retrieveSensorStatus(const SensorQuery& query) override;
    // LobbyResult retrieveEnvironmentData(const EnvironmentQuery& query) override;
    LobbyResult<SensorQuery> retrieveLiveCameraFrame(const FrameQuery& query) override; 
    LobbyResult<SensorQuery> retrieveHistoricalCameraFootage(const HistoricalVideoQuery& query) override;
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

    // // ================= 配置 =================
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

    void sendWashCarResult(const Command& cmd, const WashCarOperation& op, const WashCarOperationResult& result);

    // 监控洗车状态
    void startWashCarStatusMonitor(const WashCarOperation& operation);
    void stopWashCarStatusMonitor();
    void onWashCarStatusTimer(const WashCarOperation& operation);

private:

    IDeviceService& m_deviceService; // 设备

    ISafetyService& m_safetykService; // 安全
     
    ICommandService& m_commandService; //命令

    IWashCarSercvice& m_washcarServiece; // 洗车

    // IDetectionService& m_detectionService; // ai检测

    // FrameBuffer& m_FrameBuffer;         //帧缓冲对象

    // EquipmentStatusBuffer& m_equipmentBuffer;//设备状态缓冲对象

    DeviceStatusCache& deviceStatusCache_;//设备状态缓存
    ITimer& m_Timer; //定时器



};