#pragma once
#include "data_layer/command/command_object.h"
#include "business_layer/command/command_object.h"
#include <mutex>
#include "common/sqlite/database_manager.h"



class ICommandDao{
public:
    virtual ~ICommandDao() = default;

    virtual bool insertCommand(const CommandEntity& cmd) = 0;


    virtual bool updateCommandState(const std::string& cmdId,CommandState state) = 0;

    virtual CommandEntity getCommand(const std::string& cmdId) = 0;

    virtual CommandEntity getCommandbyType(const CommandType& type) = 0;

    virtual std::vector<CommandEntity> getDeviceCommands(const std::string& deviceId) = 0;

};



//记录命令任务的保存  所有命令历史
class CommandDao : public ICommandDao{

public:
    CommandDao(sqlite3* db):m_db(db){}

    ~CommandDao();

    bool insertCommand(const CommandEntity& cmd) override;

    bool updateCommandState(const std::string& cmdId,CommandState state) override;
    

    CommandEntity getCommand(const std::string& cmdId) override;

    CommandEntity getCommandbyType(const CommandType& type) override;

    std::vector<CommandEntity> getDeviceCommands(const std::string& deviceId) override;

private:

    sqlite3* m_db;
    std::mutex m_mutex;

private:
  
};