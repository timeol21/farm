#include "business_layer/command/mqtt/message_buffer.h"
#include "common/log/log_manager.h"
#include <algorithm>
#include <thread>
#include <functional>

#ifdef _WIN32
#include <sqlite3.h>
#else
#include <sqlite3.h>
#endif

MessageBuffer::MessageBuffer() : db_(nullptr) {
    log("INFO", "MessageBuffer initialized");
}

MessageBuffer::~MessageBuffer() {
    log("INFO", "MessageBuffer destroyed");
}

MessageBuffer& MessageBuffer::getInstance() {
    static MessageBuffer instance;
    return instance;
}

int64_t MessageBuffer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

bool MessageBuffer::isFull() const {
    return buffer_.size() >= MAX_BUFFER_SIZE;
}

void MessageBuffer::log(const std::string& level, const std::string& message) {
    if (logger_) {
        std::string logMsg = "[" + level + "] MessageBuffer: " + message;
        logger_(logMsg);
    }
}

bool MessageBuffer::addMessage(MessageType type, const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t timestamp = getCurrentTimestamp();
    std::string typeStr = messageTypeToString(type);
    log("INFO", "Adding message: type=" + typeStr + ", topic=" + topic);

    // 缓冲区满时保存最旧消息到数据库。
    // 本函数已持有 mutex_，必须走 saveToDatabaseLocked；
    // 调 saveToDatabase 会在同一线程二次加锁非递归 mutex，程序直接冻死。
    if (isFull() && !buffer_.empty()) {
        Message& oldest = buffer_.front();
        saveToDatabaseLocked(oldest.type, oldest.topic, oldest.payload, oldest.timestamp);
    }

    // 去重统一在 saveToDatabaseLocked 按 msg_type + msg_topic 做(同类型只留最新),不再比对 payload 字节。
    auto it = latestIndex_.find(type);
    if (it != latestIndex_.end() && it->second < buffer_.size()) {
        buffer_[it->second] = Message(type, topic, payload, timestamp, false);
        return true;
    }

    // 添加新消息
    buffer_.push_back(Message(type, topic, payload, timestamp, false));
    latestIndex_[type] = buffer_.size() - 1;
    return true;
}

bool MessageBuffer::getNext(Message& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (buffer_.empty()) {
        return false;
    }

    message = buffer_.front();
    buffer_.erase(buffer_.begin());
    
    // 更新索引
    latestIndex_.clear();
    for (size_t i = 0; i < buffer_.size(); ++i) {
        latestIndex_[buffer_[i].type] = i;
    }
    return true;
}

bool MessageBuffer::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.empty();
}

size_t MessageBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

void MessageBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    latestIndex_.clear();
}

size_t MessageBuffer::removeByType(MessageType type) {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t removed = 0;
    auto it = std::remove_if(buffer_.begin(), buffer_.end(),
        [type, &removed](const Message& msg) {
            return msg.type == type;
        });

    removed = std::distance(it, buffer_.end());
    buffer_.erase(it, buffer_.end());

    latestIndex_.erase(type);
    for (size_t i = 0; i < buffer_.size(); ++i) {
        latestIndex_[buffer_[i].type] = i;
    }
    return removed;
}

void MessageBuffer::saveToDatabase(MessageType type, const std::string& topic, const std::string& payload, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    saveToDatabaseLocked(type, topic, payload, timestamp);
}

void MessageBuffer::saveToDatabaseLocked(MessageType type, const std::string& topic, const std::string& payload, int64_t timestamp) {
    if (!db_) {
        log("ERROR", "Database not set");
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    std::string typeStr = messageTypeToString(type);

    // 状态类消息只保留同 topic 的最新一条：变化/状态/定时都是状态快照，旧的没有保留价值。
    // 故障不在这里合并 —— 断网期间每条故障都如实入库,重连时由 MqttService::handleReconnected
    // 调 collapseUnsentByType 收敛成最新一条。口径留在业务侧,云端策略变了不用动数据库层。
    // 命令结果全部保留。
    if (type == MessageType::TYPE_VALUE_CHANGE || type == MessageType::TYPE_WASHCAR_STATUS ||
        type == MessageType::TYPE_TIMER_REPORT) {
        const char* replaceSql = "DELETE FROM mqtt_messages WHERE sent = 0 AND msg_type = ? AND msg_topic = ?";
        sqlite3_stmt* replace = nullptr;
        if (sqlite3_prepare_v2(db, replaceSql, -1, &replace, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(replace, 1, typeStr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(replace, 2, topic.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(replace);
            sqlite3_finalize(replace);
        }
    }

    // SQL语句需要包含 topic 字段
    const char* sql = "INSERT INTO mqtt_messages (msg_type, msg_topic, payload, create_time, sent) VALUES (?, ?, ?, ?, 0)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        log("ERROR", "Failed to prepare insert: " + std::string(sqlite3_errmsg(db)));
        return;
    }

    sqlite3_bind_text(stmt, 1, typeStr.c_str(), -1, nullptr);
    sqlite3_bind_text(stmt, 2, topic.c_str(), -1, nullptr);
    sqlite3_bind_text(stmt, 3, payload.c_str(), -1, nullptr);
    sqlite3_bind_int64(stmt, 4, timestamp);

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        log("INFO", "Saved to DB: type=" + typeStr);
    }

    sqlite3_finalize(stmt);
}

size_t MessageBuffer::loadFromDatabase(size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return 0;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    size_t loaded = 0;

    // SQL查询需要包含 topic 字段
    const char* sql = "SELECT id, msg_type, msg_topic, payload, create_time FROM mqtt_messages WHERE sent = 0 ORDER BY create_time ASC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(maxCount));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t dbId = sqlite3_column_int64(stmt, 0);
        std::string typeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string topic = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        int64_t timestamp = sqlite3_column_int64(stmt, 4);

        MessageType type = stringToMessageType(typeStr);
        Message msg(type, topic, payload, timestamp, true);
        msg.dbId = dbId;
        buffer_.push_back(msg);
        loaded++;
    }

    sqlite3_finalize(stmt);

    // 更新索引
    latestIndex_.clear();
    for (size_t i = 0; i < buffer_.size(); ++i) {
        if (latestIndex_.find(buffer_[i].type) == latestIndex_.end()) {
            latestIndex_[buffer_[i].type] = i;
        }
    }

    log("INFO", "Loaded " + std::to_string(loaded) + " from DB");
    return loaded;
}

void MessageBuffer::deleteFromDatabase(int64_t dbId) {
    if (!db_) {
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    const char* sql = "DELETE FROM mqtt_messages WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, dbId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// 同类型的未发消息只留 id 最大的一条,其余直接删掉(不标记为已发 —— 它们根本没发出去)。
// 断网期间攒下的中间状态对云端没有意义:故障报文是完整状态表,最新一条已经涵盖全部点位。
void MessageBuffer::collapseUnsentByType(MessageType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    const std::string typeStr = messageTypeToString(type);
    const char* sql =
        "DELETE FROM mqtt_messages WHERE sent = 0 AND msg_type = ? "
        "AND id <> (SELECT MAX(id) FROM mqtt_messages WHERE sent = 0 AND msg_type = ?)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, typeStr.c_str(), -1, nullptr);
        sqlite3_bind_text(stmt, 2, typeStr.c_str(), -1, nullptr);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            const int dropped = sqlite3_changes(db);
            if (dropped > 0) {
                log("INFO", "Collapsed " + std::to_string(dropped)
                    + " older unsent " + typeStr + " messages");
            }
        }
        sqlite3_finalize(stmt);
    }
}

void MessageBuffer::cleanExpiredMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    int64_t now = getCurrentTimestamp();
    int64_t sentExpireTime = now - 3600 * 1000;  // 已发送：1小时
    int64_t unsentExpireTime = now - 24 * 3600 * 1000;  // 未发送：24小时

    const char* sqlSent = "DELETE FROM mqtt_messages WHERE sent = 1 AND create_time < ?";
    const char* sqlUnsent = "DELETE FROM mqtt_messages WHERE sent = 0 AND create_time < ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sqlSent, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, sentExpireTime);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db, sqlUnsent, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, unsentExpireTime);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void MessageBuffer::markAsSent(int64_t dbId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return;
    }

    sqlite3* db = static_cast<sqlite3*>(db_);
    const char* sql = "UPDATE mqtt_messages SET sent = 1 WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, dbId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string MessageBuffer::messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::TYPE_TIMER_REPORT:    return "timer_report";
        case MessageType::TYPE_VALUE_CHANGE:   return "value_change";
        case MessageType::TYPE_FAULT_REPORT:   return "fault_report";
        case MessageType::TYPE_WASHCAR_STATUS:  return "washcar_status";
        case MessageType::TYPE_WASHCAR_RESULT:  return "washcar_result";
        case MessageType::TYPE_SOLENOID_RESULT: return "solenoid_result";
        case MessageType::TYPE_POINT_RESULT: return "point_result";
        case MessageType::TYPE_ALARM_CLEAR_RESULT: return "alarm_clear_result";
        default:                               return "other";
    }
}

MessageBuffer::MessageType MessageBuffer::stringToMessageType(const std::string& typeStr) {
    if (typeStr == "timer_report")       return MessageType::TYPE_TIMER_REPORT;
    if (typeStr == "value_change")      return MessageType::TYPE_VALUE_CHANGE;
    if (typeStr == "fault_report")      return MessageType::TYPE_FAULT_REPORT;
    if (typeStr == "washcar_status")    return MessageType::TYPE_WASHCAR_STATUS;
    if (typeStr == "washcar_result")    return MessageType::TYPE_WASHCAR_RESULT;
    if (typeStr == "solenoid_result")   return MessageType::TYPE_SOLENOID_RESULT;
    if (typeStr == "point_result")     return MessageType::TYPE_POINT_RESULT;
    if (typeStr == "alarm_clear_result") return MessageType::TYPE_ALARM_CLEAR_RESULT;
    return MessageType::TYPE_OTHER;
}

void MessageBuffer::setDatabase(void* db) {
    db_ = db;
    if (db_) {
        log("INFO", "Database connection set");
    }
}

void MessageBuffer::setLogger(std::function<void(const std::string&)> logger) {
    logger_ = logger;
}
