#include "data_layer/command/command_dao.h"
// #include "common/sqlite/sqlite3.h"
#include "data_layer/command/command_object.h"            
#include "common/log/log_manager.h"
CommandDao::~CommandDao(){
    // if(m_db)
    // {
    //     sqlite3_close(m_db);
    //     m_db = nullptr;
    // }
}                  

bool CommandDao::insertCommand(const CommandEntity& cmd) {
    
    std::ostringstream oss_start;
    oss_start << "开始执行命令插入操作，命令ID：" << cmd.cmdId<< "，设备ID：" << cmd.deviceId;
    COMMAND_LOG_INFO(oss_start.str());
   
    if (cmd.cmdId.empty()) {
        std::ostringstream oss_err;
        oss_err << "命令插入失败：命令ID不能为空，设备ID：" << cmd.deviceId;
        COMMAND_LOG_ERROR(oss_err.str());
        return false;
    }

    if (cmd.deviceId.empty()) {
        std::ostringstream oss_err;
        oss_err << "命令插入失败：设备ID不能为空，命令ID：" << cmd.cmdId;
        COMMAND_LOG_ERROR(oss_err.str());
        return false;
    }

    std::ostringstream oss_debug;
    oss_debug << "命令详细信息：\n"
              << "  命令ID    ：" << cmd.cmdId << "\n"
              << "  设备ID    ：" << cmd.deviceId << "\n"
              << "  命令类型  ：" << static_cast<int>(cmd.type) << "\n"
              << "  命令状态  ：" << static_cast<int>(cmd.state) << "\n"
              << "  命令内容  ：" << cmd.content << "\n"
              << "  创建时间  ：" << cmd.createTime << "\n"
              << "  更新时间  ：" << cmd.updateTime;
    COMMAND_LOG_DEBUG(oss_debug.str());

    
    try {
        bool insert_success = true;

        if (!insert_success) {
            std::ostringstream oss_fail;
            oss_fail << "命令插入数据存储失败，命令ID：" << cmd.cmdId;
            COMMAND_LOG_ERROR(oss_fail.str());
            return false;
        }
    } catch (...) {
        std::ostringstream oss_exc;
        oss_exc << "命令插入发生未知异常，命令ID：" << cmd.cmdId;
        COMMAND_LOG_ERROR(oss_exc.str());
        return false;
    }

    std::ostringstream oss_succ;
    oss_succ << "命令插入成功！命令ID：" << cmd.cmdId
              << "，设备ID：" << cmd.deviceId;
    COMMAND_LOG_INFO(oss_succ.str());

    return true;

}                  


bool CommandDao::updateCommandState(const std::string& cmdId,CommandState state) {
    // std::lock_guard<std::mutex> lock(m_mutex);

    

    return true;

}
CommandEntity CommandDao::getCommand(const std::string& cmdId) {
    // std::lock_guard<std::mutex> lock(m_mutex);

    

    return CommandEntity{};
}                  

CommandEntity CommandDao::getCommandbyType(const CommandType& type) {
    CommandEntity entity;
    return entity;
}                  

std::vector<CommandEntity> CommandDao::getDeviceCommands(const std::string& deviceId) {
    // std::lock_guard<std::mutex> lock(m_mutex);

    

    return {};
}     
