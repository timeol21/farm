#pragma once
#include <string>
#include "common/sqlite/sqlite3.h"
class DatabaseManager {
public:
    static DatabaseManager& instance();

    bool init(const std::string& folderPath);

    sqlite3* getAIDB();

    sqlite3* getCommandDB();

    ~DatabaseManager() ;

private:
    sqlite3* commandDb = nullptr;
    sqlite3* aiDb  = nullptr;


    DatabaseManager() = default;

   

};