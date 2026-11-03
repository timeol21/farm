#pragma once
#include <string>
#include "common/sqlite/sqlite3.h"
class DatabaseManager {
public:
    static DatabaseManager& instance();

    // folderPath 为数据目录，不存在会先创建；下面放 command.db 和 ai.db 两个库。
    bool init(const std::string& folderPath);

    sqlite3* getAIDB();

    sqlite3* getCommandDB();

    ~DatabaseManager() ;

private:
    sqlite3* commandDb = nullptr;
    sqlite3* aiDb  = nullptr;


    DatabaseManager() = default;

   

};
