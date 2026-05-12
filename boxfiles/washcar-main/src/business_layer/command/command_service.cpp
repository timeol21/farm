#include "business_layer/command/command_service.h"
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
    CommandEntity entity = CommandEntity(cmd);

    // 1 保存数据库
    commandDao.insertCommand(entity);

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



// 实现接口：发送命令结果到MQTT云端
void CommandService::sendCommandResultToMqtt(const std::string& topic, const std::string& msg){
    if (m_mqttService)
    {
        m_mqttService->publish(topic, msg);
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

