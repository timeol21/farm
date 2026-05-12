#pragma once
#include <string>
#include "business_layer/lobby/lobby_object.h"
#include "data_layer/command/command_object.h"
#include <vector>
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
    TYPE_WASHCAR_START,  // 启动洗车
    TYPE_WASHCAR_STOP,   // 停止洗车
    TYPE_WASHCAR_RESET,  // 复位洗车
    TYPE_OTHER           // 其他业务命令
};


class Command {
public:
	Command(std::string id, CommandType type, std::string devId);
        
    
	~Command() = default;

    //1.打开电磁阀命令
    static Command createOperateSolenoid(const SolenoidValveOperation& solenoid);

    //2.下载历史视频命令
    static Command createHistoricalVideo(DownloadHistoricalVideo& download);

    //3.根据电磁阀操作返回结果创建命令
    static Command createOperateSolenoidResult(const DeviceOperationResult& solenoid,const SolenoidValveOperation& operation);

    //4.洗车命令
    static Command createWashCarStart(const WashCarOperation& operation);
    static Command createWashCarStop(const WashCarOperation& operation);
    static Command createWashCarReset(const WashCarOperation& operation);
    

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