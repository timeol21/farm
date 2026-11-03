#include <stdio.h>
#include <stdint.h>
#include "common/sqlite/sqlite3.h"

// 回调函数：处理查询结果
static int callback(void *data, int argc, char **argv, char **azColName) {
    for (int i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

int main() {
    sqlite3 *db;
    char *errMsg;
    int rc;

    // 1. 打开/创建数据库
    rc = sqlite3_open("/home/lin/Desktop/Framework/include/common/database/command.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // 2. 创建 command 表（自增主键 + 匹配你给出的所有字段）
    const char *createSql =
        "CREATE TABLE IF NOT EXISTS command ("
        "id          INTEGER PRIMARY KEY AUTOINCREMENT,"  // 自增主键（替代 cmdId）
        "cmdId       TEXT    NOT NULL,"                    // 命令唯一ID
        "deviceId    TEXT    NOT NULL,"                    // 设备ID
        "type        INTEGER NOT NULL,"                    // 命令类型（枚举存int）
        "state       INTEGER NOT NULL,"                    // 命令状态（枚举存int）
        "content     TEXT,"                                // 命令内容
        "createTime  INTEGER NOT NULL,"                    // 创建时间戳 int64
        "updateTime  INTEGER NOT NULL"                     // 更新时间戳 int64
        ");";

    rc = sqlite3_exec(db, createSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建表失败: %s\n", errMsg);
        sqlite3_free(errMsg);
    } else {
        printf("表 command 创建成功/已存在\n");
    }

    // 3. 插入测试数据
    const char *insertSql =
        "INSERT INTO command (cmdId, deviceId, type, state, content, createTime, updateTime)"
        " VALUES ('CMD_20260317_001', 'DEV_001', 1, 0, 'open device', 1741234567890, 1741234567890);";

    rc = sqlite3_exec(db, insertSql, NULL, 0, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "插入数据失败: %s\n", errMsg);
        sqlite3_free(errMsg);
    } else {
        printf("测试数据插入成功\n");
    }

    // 4. 查询数据
    const char *selectSql = "SELECT * FROM command;";
    printf("\n===== command 表查询结果 =====\n");
    rc = sqlite3_exec(db, selectSql, callback, 0, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "查询失败: %s\n", errMsg);
        sqlite3_free(errMsg);
    }

    // 5. 关闭数据库
    sqlite3_close(db);
    return 0;
}