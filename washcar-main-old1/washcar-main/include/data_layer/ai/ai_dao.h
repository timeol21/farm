#pragma once

#include "business_layer/detection/detection_object.h"
#include "common/sqlite/database_manager.h"

#include <cstdint>
#include <string>
#include <vector>

// AI 检测结果的数据访问层
// 一条检测结果对应 ai_results 表的一行记录
class IModuleDao {
public:
    virtual ~IModuleDao() = default;

    // 启动时建表, 幂等
    virtual bool initAIDao() = 0;

    // 一条一存: 检测完成时立即写入
    virtual bool saveDetectionResult(const std::string& cameraId, const DetectionResult& result) = 0;

    // 删 retentionDays 天前的记录, 启动时 + 每次 save 后调一次
    virtual bool cleanupExpiredResults(int retentionDays) = 0;

    // 只保留最近 keepRows 条(超出部分删最旧的)。条数封顶才是体积的硬上限:
    // 删掉的行空间会被后续插入复用, 库文件不会一直涨
    virtual bool cleanupOverflowResults(int keepRows) = 0;

    // 对外查询: 帧时间戳区间 [startMs, endMs] 内某 cameraId 的检测结果, 按帧时间戳倒序
    virtual std::vector<DetectionResult> queryDetectionResults(
        const std::string& cameraId, int64_t startMs, int64_t endMs, int limit) = 0;
};

class ModuleDao : public IModuleDao {
public:
    explicit ModuleDao(DatabaseManager& db);
    ~ModuleDao() override;

    bool initAIDao() override;
    bool saveDetectionResult(const std::string& cameraId, const DetectionResult& result) override;
    bool cleanupExpiredResults(int retentionDays) override;
    bool cleanupOverflowResults(int keepRows) override;
    std::vector<DetectionResult> queryDetectionResults(
        const std::string& cameraId, int64_t startMs, int64_t endMs, int limit) override;

private:
    DatabaseManager& m_db;
};
