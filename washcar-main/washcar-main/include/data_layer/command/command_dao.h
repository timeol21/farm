#pragma once
#include "data_layer/command/command_object.h"
#include <mutex>
#include "common/sqlite/database_manager.h"

// 命令台账: 只做"记一条", 没有查询/状态机需求(需要历史直接查库)
class ICommandDao {
public:
    virtual ~ICommandDao() = default;

    virtual bool insertCommand(const CommandEntity& cmd) = 0;
};

class CommandDao : public ICommandDao {
public:
    explicit CommandDao(sqlite3* db) : m_db(db) {}
    ~CommandDao();

    bool insertCommand(const CommandEntity& cmd) override;

private:
    sqlite3* m_db;
    std::mutex m_mutex;
};
