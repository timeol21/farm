#include "common/sqlite/database_manager.h"
#include "common/log/log_manager.h"
#include <filesystem>



DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager instance;
    return instance;
}


bool DatabaseManager::init(const std::string& folderPath)
{
    std::error_code ec;
    std::filesystem::create_directories(folderPath, ec);
    if (ec) {
        LOG_ERROR("[Database] create storage directory failed: " + ec.message());
        return false;
    }

    // 打开数据库
    if (sqlite3_open((folderPath + "/command.db").c_str(), &commandDb) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_open((folderPath + "/ai.db").c_str(), &aiDb) != SQLITE_OK) {
        return false;
    }

    // 初始化 mqtt_messages 表（用于消息缓冲持久化）
    const char* createMqttMessagesTable = R"(
        CREATE TABLE IF NOT EXISTS mqtt_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            msg_type TEXT NOT NULL,
            msg_topic TEXT NOT NULL,
            payload TEXT NOT NULL,
            create_time INTEGER NOT NULL,
            sent INTEGER DEFAULT 0,
            dedup_key TEXT
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(commandDb, createMqttMessagesTable, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        // 表创建失败（可能是语法错误），记录错误
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }

    // 旧数据库兼容迁移：重复执行时 SQLITE_ERROR 只表示列已存在。
    sqlite3_exec(commandDb, "ALTER TABLE mqtt_messages ADD COLUMN dedup_key TEXT;",
                 nullptr, nullptr, nullptr);

    // command 服务的命令台账。字段顺序与 CommandDao::insertCommand 的绑定顺序一致。
    // 历史上这张表从来没建过, insertCommand 一直 prepare_failed(日志里只有一行 WARNING)。
    const char* createCommandsTable = R"(
        CREATE TABLE IF NOT EXISTS commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            cmd_id TEXT NOT NULL,
            device_id TEXT,
            type INTEGER NOT NULL,
            state INTEGER NOT NULL,
            payload TEXT,
            create_time INTEGER NOT NULL,
            update_time INTEGER NOT NULL
        );
    )";
    if (sqlite3_exec(commandDb, createCommandsTable, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    // 创建索引（加速查询）
    const char* createIndex1 = "CREATE INDEX IF NOT EXISTS idx_msg_type ON mqtt_messages(msg_type);";
    const char* createIndex2 = "CREATE INDEX IF NOT EXISTS idx_create_time ON mqtt_messages(create_time);";
    const char* createIndex3 = "CREATE INDEX IF NOT EXISTS idx_sent ON mqtt_messages(sent);";
    // 补发按 (sent, id) 取，"sent=0 且 id 升序"走这一个索引就够
    const char* createIndex4 = "CREATE INDEX IF NOT EXISTS idx_msg_sent_id ON mqtt_messages(sent, id);";

    sqlite3_exec(commandDb, createIndex1, nullptr, nullptr, nullptr);
    sqlite3_exec(commandDb, createIndex2, nullptr, nullptr, nullptr);
    sqlite3_exec(commandDb, createIndex3, nullptr, nullptr, nullptr);
    sqlite3_exec(commandDb, createIndex4, nullptr, nullptr, nullptr);

    // 合库版本(单文件 washcar.db)留下的孤儿文件:确认没数据就删掉,还有数据只告警不动,
    // 免得升级时把现场还等着补发的消息弄丢。
    const std::string legacy = (std::filesystem::path(folderPath) / "washcar.db").string();
    std::error_code fsEc;
    if (std::filesystem::exists(legacy, fsEc)) {
        sqlite3* oldDb = nullptr;
        bool allEmpty = false;
        if (sqlite3_open(legacy.c_str(), &oldDb) == SQLITE_OK) {
            allEmpty = true;
            for (const char* table : {"mqtt_messages", "ai_results"}) {
                const std::string sql = std::string("SELECT count(*) FROM ") + table + ";";
                sqlite3_stmt* q = nullptr;
                if (sqlite3_prepare_v2(oldDb, sql.c_str(), -1, &q, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(q) == SQLITE_ROW && sqlite3_column_int64(q, 0) > 0) {
                        allEmpty = false;
                    }
                    sqlite3_finalize(q);
                }
            }
        }
        if (oldDb) sqlite3_close(oldDb);

        if (allEmpty && std::filesystem::remove(legacy, fsEc)) {
            LOG_WARNING("[Database] removed empty legacy file: " + legacy);
        } else if (!allEmpty) {
            LOG_WARNING("[Database] legacy " + legacy + " 仍有数据，未删除，请手工确认");
        }
    }

    LOG_INFO("[Database] command=" + folderPath + "/command.db ai=" + folderPath + "/ai.db");
    return true;
}

sqlite3* DatabaseManager::getAIDB()
{
    return aiDb;
}
sqlite3* DatabaseManager::getCommandDB(){
    return commandDb;
}


DatabaseManager:: ~DatabaseManager()
{
    if(aiDb) sqlite3_close(aiDb);

    if(commandDb) sqlite3_close(commandDb);
}
