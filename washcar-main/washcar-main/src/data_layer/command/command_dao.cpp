#include "data_layer/command/command_dao.h"
#include "common/sqlite/sqlite3.h"
#include "data_layer/command/command_object.h"
#include "common/log/log_manager.h"

CommandDao::~CommandDao() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool CommandDao::insertCommand(const CommandEntity& cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_db) {
        LOG_WARNING(std::string("[CommandDao] CMD_PERSIST_FAIL reason=no_db, cmdId=") + cmd.cmdId);
        return false;
    }

    // state 列是历史 schema 的一部分(NOT NULL), 写 0 占位; 命令状态已不再建模
    const char* sql =
        "INSERT INTO commands "
        "(cmd_id,device_id,type,state,payload,create_time,update_time)"
        "VALUES(?,?,?,?,?,?,?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_WARNING(std::string("[CommandDao] CMD_PERSIST_FAIL reason=prepare_failed, cmdId=") + cmd.cmdId);
        return false;
    }

    sqlite3_bind_text(stmt, 1, cmd.cmdId.c_str(), -1, nullptr);
    sqlite3_bind_text(stmt, 2, cmd.deviceId.c_str(), -1, nullptr);
    sqlite3_bind_int(stmt, 3, static_cast<int>(cmd.type));
    sqlite3_bind_int(stmt, 4, 0);
    sqlite3_bind_text(stmt, 5, cmd.content.c_str(), -1, nullptr);
    sqlite3_bind_int64(stmt, 6, cmd.createTime);
    sqlite3_bind_int64(stmt, 7, cmd.updateTime);

    const bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!success) {
        LOG_WARNING(std::string("[CommandDao] CMD_PERSIST_FAIL reason=step_failed, cmdId=") + cmd.cmdId);
    }
    return success;
}
