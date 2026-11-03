#include "business_layer/command/command_service.h"
#include "business_layer/command/mqtt/mqtt_service.h"
#include "common/log/log_manager.h"
#include <iostream>

CommandService::CommandService(NetworkService* mqttService, CommandDao& cmdDao): m_mqttService(mqttService),commandDao(cmdDao) {}

void CommandService::immitDependence(NetworkService& mqttService){
	m_mqttService = &mqttService;
    if(m_mqttService){
        executePendingCommands();
    }
}

CommandService::~CommandService() {
    stopPolling();

}

void CommandService::executeCommand(const Command& cmd)
{
    LOG_INFO(std::string("[CommandService] CMD_ACCEPT cmdId=") + cmd.getCmdId()
        + ", cmdType=" + std::to_string((int)cmd.getCmdType())
        + ", deviceId=" + cmd.getDeviceId());
    CommandEntity entity = CommandEntity(cmd);

    // 1 保存数据库
    if (!commandDao.insertCommand(entity)) {
        LOG_ERROR(std::string("[CommandService] CMD_PERSIST_FAIL cmdId=") + entity.cmdId
            + ", cmdType=" + std::to_string((int)entity.type)
            + ", deviceId=" + entity.deviceId);
    }

    // 2 放入缓存
    // Command newCmd = cmd;
    // commandCache.emplace(cmd.getCmdId(), cmd);
}

// 实现接口：执行待执行命令
void CommandService::executePendingCommands() {
    if(running){
        return;
    }

    running = true;

    cmdPolling = std::thread(&CommandService::CommmandPollingCondition,this);

}

// 实现接口：查询本地命令任务状态（在缓冲里面的）
CommandState CommandService:: getCommandState(const std::string& cmdId) {
	//在缓冲里面的去查看相应的当前命令的状态
	auto it = commandCache.find(cmdId);

    if (it != commandCache.end())
    {
        return it->second.getCmdState();
    }

    CommandEntity entity = commandDao.getCommand(cmdId);

    return entity.state;
}

// 实现接口：获取指定设备的所有命令状态
DeviceCommands CommandService::getDeviceCommandStates(const std::string& deviceId)
{
    DeviceCommands result;

    auto cmds = commandDao.getDeviceCommands(deviceId);

    for (auto& cmd : cmds)
    {
        // result.commands.push_back(cmd);
    }

    return result;
}

CommandState CommandService::getCommandState(const CommandType& type)
{
    CommandEntity cmd = commandDao.getCommandbyType(type);

    return cmd.state;
}



// 实现接口：发送命令结果到MQTT云端（直接使用完整 topic）
void CommandService::sendCommandResultToMqtt(const std::string& topic, const std::string& msg,
                                          MessageBuffer::MessageType msgType) {
    LOG_INFO(std::string("[CommandService] CMD_REPORT_SEND topic=") + topic
        + ", bytes=" + std::to_string(msg.size())
        + ", type=" + std::to_string((int)msgType));
    if (m_mqttService) {
        // 使用 publishWithBuffer 进行消息缓冲
        MqttService* mqttService = static_cast<MqttService*>(m_mqttService);
        mqttService->publishWithBuffer(topic, msg, msgType);
    } else {
        LOG_ERROR(std::string("[CommandService] CMD_REPORT_DROP topic=") + topic
        + ", reason=service_null, type=" + std::to_string((int)msgType));
    }
}

// 发送洗车机上报（topicSuffix 由 MqttService 内部补 flexem/boxNo）
void CommandService::sendWashCarReportToMqtt(const std::string& topicSuffix, const std::string& msg,
                                             MessageBuffer::MessageType msgType) {
    LOG_INFO(std::string("[CommandService] CMD_REPORT_SEND topicSuffix=") + topicSuffix
        + ", bytes=" + std::to_string(msg.size())
        + ", type=" + std::to_string((int)msgType));
    if (m_mqttService) {
        MqttService* mqttService = static_cast<MqttService*>(m_mqttService);
        mqttService->publishMessage(topicSuffix, msg, msgType);
    } else {
        LOG_ERROR(std::string("[CommandService] CMD_REPORT_DROP topicSuffix=") + topicSuffix
        + ", reason=service_null, type=" + std::to_string((int)msgType));
    }
}

// 实现接口：更新命令状态
void CommandService::updateCommandState(const std::string& cmdId, CommandState newState) {
	// auto it = commandCache.find(cmdId);

    // if (it == commandCache.end())
    // {
    //     return ;
    // }

    // it->second.setCmdState(newState);

    commandDao.updateCommandState(cmdId,newState);

}


// private

void CommandService::CommmandPollingCondition(){
    while(running){
        long long now = std::time(nullptr);

        {
            std::lock_guard<std::mutex> lock(commandMutex);
                for(auto it =commandCache.begin(); it != commandCache.end();){
                Command& cmd = it->second;

                bool remove = false;

                //超时检测
                auto duration = now - cmd.getCreateTime();

                if(duration >= 20 * 60 && (cmd.getCmdState() == CommandState::STATE_EXECUTING ||cmd.getCmdState() == CommandState::STATE_PENDING)){
                    commandDao.updateCommandState(cmd.getCmdId(), CommandState::STATE_TIMEOUT);
                    remove = true;
                }

                if(remove){
                    it = commandCache.erase(it);
                } 
                else
                {
                    ++it;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        
    }

}

void CommandService::stopPolling()
{
    if (!running)
        return;

    running = false;

    if (cmdPolling.joinable())
    {
        cmdPolling.join();
    }
}

