#pragma once
#include <string>
#include <vector>
#include <chrono>
class CommandEntity;
enum class CommandState{

	STATE_IDLE,  //空闲
	STATE_PENDING,     // 待执行
    STATE_EXECUTING,   // 执行中
    STATE_SUCCESS,     // 执行成功
    STATE_FAILED,      // 执行失败
    STATE_TIMEOUT      // 执行超时
};

enum class CommandType
{
    TYPE_SOLENOID_OPEN,  // 打开电磁阀
    TYPE_SOLENOID_CLOSE, // 关闭电磁阀
    TYPE_CAMERA_HISTORY, // 查看摄像头历史视频
    TYPE_DEVICE_READ,    // 读取设备状态
    TYPE_DEVICE_CTRL,    // 控制设备（通用）
    TYPE_FRAME_EXTRACT,  // 实时提取帧（流媒体）
    TYPE_OTHER           // 其他业务命令
};


class Command {
public:
	Command(std::string id, CommandType type, std::string devId,CommandState state);
        
    Command() = default;

	~Command() = default;

    // //1.打开电磁阀命令
    // static Command createOperateSolenoid(const SolenoidValveOperation& solenoid);

    // //2.下载历史视频命令
    // static Command createHistoricalVideo(DownloadHistoricalVideo& download);

    // //3.根据电磁阀操作返回结果创建命令
    // static Command createOperateSolenoidResult(const int result,const SolenoidValveOperation& operation);

    // static Command createRetrieveSolenoidStatusResult(const SolenoidStatus& query);

    // static Command createRetrieveTempHumidSensorStatusResult(const TempHumidSensorStatus& query);

    // static Command createRetrieveInfraredSensorStatusResult(const InfraredSensorStatus& query);

    // static Command createRetrieveSmokeDetectorStatusResult(const SmokeDetectorStatus& query);

    // static Command createRetrieveWaterLevelSensorStatusResult(const WaterLevelSensorStatus& query);
    
    bool isEmpty() const;
    std::string getCmdId() const;
    CommandType getCmdType() const;
    CommandState getCmdState() const;
    std::string getDeviceId() const;
    std::string getCmdContent() const;
    long long getCreateTime() const;
    long long getExecuteTime() const;

    void setCmdState(CommandState state);
private:
    //创建cmdId;
    static std::string createCmdId();//CMD_20260317_001 这样的
private:
    

	std::string cmdId;
	CommandType cmdType;
	CommandState cmdState;
	std::string deviceId;
	std::string cmdContent;
	long long createTime;
	long long executeTime;
};

class DeviceCommands{
public:    
    DeviceCommands() = default;
    ~DeviceCommands() = default;
    
private:
    std::vector<CommandEntity> commmands;
};