#include "data_layer/ai/ai_dao.h"
#include "common/log/log_manager.h"

#include <nlohmann/json.hpp>
#include <ctime>

using json = nlohmann::json;

namespace {

constexpr const char* kCreateAiResultsTable = R"(
    CREATE TABLE IF NOT EXISTS ai_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        camera_id TEXT NOT NULL,
        frame_timestamp INTEGER NOT NULL,
        payload TEXT NOT NULL,
        create_time INTEGER NOT NULL
    );
)";
constexpr const char* kIndexCamera = "CREATE INDEX IF NOT EXISTS idx_ai_results_camera ON ai_results(camera_id);";
constexpr const char* kIndexTime = "CREATE INDEX IF NOT EXISTS idx_ai_results_time ON ai_results(create_time);";
// [FIX-05] 按帧时间戳查询的索引: 原 SQL 用 create_time 过滤 frame_timestamp,
//   getDetectionResultsByFrame 传的是帧时间戳 → 按帧时间戳查历史恒查不到。
constexpr const char* kIndexCameraFrame = "CREATE INDEX IF NOT EXISTS idx_ai_results_camera_frame ON ai_results(camera_id, frame_timestamp);";

constexpr int kMilliPerSecond = 1000;
constexpr int kSecondsPerDay = 86400;

}

ModuleDao::ModuleDao(DatabaseManager& db) : m_db(db) {}

ModuleDao::~ModuleDao() = default;

bool ModuleDao::initAIDao() {
    sqlite3* db = m_db.getAIDB();
    if (!db) {
        return false;
    }
    char* err = nullptr;
    if (sqlite3_exec(db, kCreateAiResultsTable, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) {
            LOG_ERROR(std::string("initAIDao: ") + err);
            sqlite3_free(err);
        }
        return false;
    }
    sqlite3_exec(db, kIndexCamera, nullptr, nullptr, nullptr);
    sqlite3_exec(db, kIndexTime, nullptr, nullptr, nullptr);
    sqlite3_exec(db, kIndexCameraFrame, nullptr, nullptr, nullptr);
    return true;
}

bool ModuleDao::saveDetectionResult(const std::string& cameraId, const DetectionResult& result) {
    sqlite3* db = m_db.getAIDB();
    if (!db) {
        return false;
    }

    json items = json::array();
    for (const auto& item : result.items) {
        items.push_back({
            {"type", static_cast<int>(item.type)},
            {"detected", item.detected},
            {"value", item.value},
            {"confidence", item.confidence},
            {"timestamp", item.timestamp},
            // 框 + 原图路径: 两者一起才还原得出"哪张图的哪个位置检出了什么"
            {"box", {item.box[0], item.box[1], item.box[2], item.box[3]}},
            {"imagePath", item.imagePath}
        });
    }
    std::string payload = items.dump();

    const char* sql =
        "INSERT INTO ai_results(camera_id, frame_timestamp, payload, create_time) VALUES(?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR(std::string("saveDetectionResult: prepare failed, cameraId=") + cameraId);
        return false;
    }

    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, result.frameTimestamp);
    sqlite3_bind_text(stmt, 3, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(std::time(nullptr)) * kMilliPerSecond);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (!ok) {
        LOG_ERROR(std::string("saveDetectionResult: step failed, cameraId=") + cameraId);
    }
    return ok;
}

bool ModuleDao::cleanupExpiredResults(int retentionDays) {
    if (retentionDays <= 0) {
        return false;
    }
    sqlite3* db = m_db.getAIDB();
    if (!db) {
        return false;
    }
    int64_t cutoffMs = (static_cast<int64_t>(std::time(nullptr))
                       - static_cast<int64_t>(retentionDays) * kSecondsPerDay) * kMilliPerSecond;

    const char* sql = "DELETE FROM ai_results WHERE create_time < ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, cutoffMs);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    int deleted = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    if (ok && deleted > 0) {
        LOG_INFO(std::string("cleanupExpiredResults: removed ") + std::to_string(deleted)
            + " rows older than " + std::to_string(retentionDays) + " days");
    }
    return ok;
}

bool ModuleDao::cleanupOverflowResults(int keepRows) {
    if (keepRows <= 0) {
        return false;
    }
    sqlite3* db = m_db.getAIDB();
    if (!db) {
        return false;
    }
    // 保留 id 最大的 keepRows 条, 其余删掉
    const char* sql = "DELETE FROM ai_results WHERE id <= (SELECT MAX(id) FROM ai_results) - ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, keepRows);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    int deleted = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    if (ok && deleted > 0) {
        LOG_INFO(std::string("cleanupOverflowResults: removed ") + std::to_string(deleted)
            + " rows, keep latest " + std::to_string(keepRows));
    }
    return ok;
}

std::vector<DetectionResult> ModuleDao::queryDetectionResults(
    const std::string& cameraId, int64_t startMs, int64_t endMs, int limit) {
    std::vector<DetectionResult> results;
    sqlite3* db = m_db.getAIDB();
    if (!db || limit <= 0) {
        return results;
    }

    const char* sql =
        "SELECT frame_timestamp, payload FROM ai_results "
        "WHERE camera_id=? AND frame_timestamp BETWEEN ? AND ? "
        "ORDER BY frame_timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, startMs);
    sqlite3_bind_int64(stmt, 3, endMs);
    sqlite3_bind_int(stmt, 4, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DetectionResult r;
        r.frameTimestamp = sqlite3_column_int64(stmt, 0);
        const unsigned char* text = sqlite3_column_text(stmt, 1);
        if (text) {
            try {
                json items = json::parse(reinterpret_cast<const char*>(text));
                for (const auto& jitem : items) {
                    DetectionResult::Item item;
                    item.type = static_cast<DetectionResult::Type>(jitem.value("type", 0));
                    item.detected = jitem.value("detected", false);
                    item.value = jitem.value("value", std::string{});
                    item.confidence = jitem.value("confidence", 0.0f);
                    item.timestamp = jitem.value("timestamp", static_cast<int64_t>(0));
                    item.imagePath = jitem.value("imagePath", std::string{});
                    if (jitem.contains("box") && jitem["box"].is_array() && jitem["box"].size() == 4) {
                        for (int k = 0; k < 4; ++k) item.box[k] = jitem["box"][k].get<float>();
                    }
                    r.items.push_back(item);
                }
            } catch (...) {
                // 跳过坏 JSON 行
            }
        }
        results.push_back(r);
    }
    sqlite3_finalize(stmt);
    return results;
}
