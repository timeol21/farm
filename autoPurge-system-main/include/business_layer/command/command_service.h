#pragma once

#include "common/application/lifecycle.h"
#include "business_layer/command/command_object.h"
#include "business_layer/command/mqtt/network_service.h"
#include "data_layer/command/command_dao.h"

class ICommandService{
public:
    virtual ~ICommandService() = default;

    // 执行指定命令（文档中"构建命令对象并执行命令"）
	virtual void executeCommand(const Command& cmd) = 0;

	// 执行命令轮询
	virtual void executePendingCommands() = 0;

	// 查询本地命令任务状态（按命令ID）
	virtual CommandState getCommandState(const std::string& cmdId) = 0; 

	// 获取指定设备的所有命令任务状态（按设备ID，文档"获取当前设备任务的状态"）
	virtual DeviceCommands getDeviceCommandStates(const std::string& deviceId) = 0;

	//实现接口：查看那种类型的命令状态
	virtual CommandState getCommandState(const CommandType& type) = 0;

	// 发送命令结果到MQTT（文档中"将消息发送出去，上报执行结果"）
	virtual void sendCommandResultToMqtt(const std::string& topic, const std::string& msg) = 0;

	// 更新命令状态（执行后同步状态到DAO）
	virtual void updateCommandState(const std::string& cmdId, CommandState newState) = 0;

};


class CommandService : public ICommandService, public ILifecycle{
public:

    CommandService(NetworkService* mqttService, ICommandDao& cmdDao);

    ~CommandService();
    


    void start() override;

   void stop() override;

    
    // 注入MQTT网络服务（动态替换，适配扩展）
	void immitDependence(NetworkService& mqttService);

    // 实现接口：执行指定命令
    void executeCommand(const Command& cmd) override;

    // 实现接口：批量执行待执行命令
    void executePendingCommands() override;

    // 实现接口：查询正在执行命令任务状态
    CommandState getCommandState(const std::string& cmdId) override;

    // 实现接口：获取指定设备的所有命令状态
    DeviceCommands getDeviceCommandStates(const std::string& deviceId) override;

    //实现接口：查看那种类型的命令状态
	CommandState getCommandState(const CommandType& type) override;


    // 实现接口：发送命令结果到MQTT
    void sendCommandResultToMqtt(const std::string& topic,const std::string& msg) override;

    // 实现接口：更新命令状态
    void updateCommandState(const std::string& cmdId, CommandState newState) override;
    
private:
    NetworkService* m_mqttService; // MQTT网络服务（文档中mqtt连接封装类）

	ICommandDao& commandDao;	// 命令DAO（文档中命令存储，持久化）

};