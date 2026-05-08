#include "common/sqlite/database_manager.h"



DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager instance;
    return instance;
}


bool DatabaseManager::init(const std::string& folderPath)
{   
    sqlite3_open((folderPath + "/command.db").c_str(), &commandDb);
    sqlite3_open((folderPath + "/ai.db").c_str(), &aiDb);
    // sqlite3_open((folderPath + "/log.db").c_str(), &logDb);
    // sqlite3_open((folderPath + "/config.db").c_str(), &configDb);

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