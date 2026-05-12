#include "data_layer/command/command_dao.h"
#include "common/sqlite/sqlite3.h"
#include "data_layer/command/command_object.h"            

CommandDao::~CommandDao(){
    if(m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}                  

bool CommandDao::insertCommand(const CommandEntity& cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql =
        "INSERT INTO commands "
        "(cmd_id,device_id,type,state,payload,create_time,update_time)"
        "VALUES(?,?,?,?,?,?,?)";
    
   sqlite3_stmt* stmt = stmt;
   
   if(sqlite3_prepare_v2(m_db,sql,-1,&stmt,nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt,1,cmd.cmdId.c_str(),-1,nullptr);
    sqlite3_bind_text(stmt,2,cmd.deviceId.c_str(),-1,nullptr);
    sqlite3_bind_int(stmt,3,(int)cmd.type);
    sqlite3_bind_int(stmt,4,(int)cmd.state);
    sqlite3_bind_text(stmt,5,cmd.content.c_str(),-1,nullptr);
    sqlite3_bind_int64(stmt,6,cmd.createTime);
    sqlite3_bind_int64(stmt,7,cmd.updateTime);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;

    sqlite3_finalize(stmt);

    return success; //这里应该是需要返回的

}                  


bool CommandDao::updateCommandState(const std::string& cmdId,CommandState state) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql =
        "UPDATE commands SET state=?,update_time=strftime('%s','now') WHERE id=?";

    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);

    sqlite3_bind_int(stmt,1,(int)state);
    sqlite3_bind_text(stmt,2,cmdId.c_str(),-1,nullptr);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;

    sqlite3_finalize(stmt);

    return success;

}
CommandEntity CommandDao::getCommand(const std::string& cmdId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "SELECT * FROM commands WHERE id=?";

    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt,1,cmdId.c_str(),-1,nullptr);

    CommandEntity cmd;

    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
        cmd.cmdId = (const char*)sqlite3_column_text(stmt,0);
        cmd.deviceId = (const char*)sqlite3_column_text(stmt,1);
        cmd.type = (CommandType)sqlite3_column_int(stmt,2);
        cmd.state = (CommandState)sqlite3_column_int(stmt,3);
        cmd.content = (const char*)sqlite3_column_text(stmt,4);
        cmd.createTime = sqlite3_column_int64(stmt,5);
        cmd.updateTime = sqlite3_column_int64(stmt,6);
    }

    sqlite3_finalize(stmt);

    return cmd;
}                  

CommandEntity CommandDao::getCommandbyType(const CommandType& type) {
    CommandEntity entity;
    return entity;
}                  

std::vector<CommandEntity> CommandDao::getDeviceCommands(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const char* sql = "SELECT * FROM commands WHERE device_id=?";

    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt,1,deviceId.c_str(),-1,nullptr);

    std::vector<CommandEntity> list;

    while(sqlite3_step(stmt) == SQLITE_ROW)
    {
        CommandEntity cmd;

        cmd.cmdId = (const char*)sqlite3_column_text(stmt,0);
        cmd.deviceId = (const char*)sqlite3_column_text(stmt,1);
        cmd.type = (CommandType)sqlite3_column_int(stmt,2);
        cmd.state = (CommandState)sqlite3_column_int(stmt,3);
        cmd.content = (const char*)sqlite3_column_text(stmt,4);
        cmd.createTime = sqlite3_column_int64(stmt,5);
        cmd.updateTime = sqlite3_column_int64(stmt,6);

        list.push_back(cmd);
    }

    sqlite3_finalize(stmt);

    return list;
}     
