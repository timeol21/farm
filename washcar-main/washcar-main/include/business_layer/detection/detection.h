#pragma once
#include "data_layer/ai/ai_dao.h"
#include "business_layer/detection/detection_object.h"
#include "business_layer/detection/detection_region.h"
#include "business_layer/detection/detection_result_cache.h"
#include "business_layer/device/real_time_frame_cache.h"
#include "common/config/config_object.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 检测服务(业务类): 管区域表、检测结果缓冲、落库与图片留证。
// 模型加载/复用交给 AiModelFactory, 单个区域的"跑模型 + 频率/去重"交给 DetectionRegion。
// 对外主接口是同步的 detectRegion(): 跑完本区域全部模型再返回, 洗车前综合判断直接用它。
class IDetectionService {
public:
    virtual ~IDetectionService() = default;

    // 指定区域检测(同步)。force=true 忽略频率间隔; 未到期的返回该区域最近一次结果。
    virtual DetectionResult detectRegion(const std::string& regionId, bool force = false) = 0;
    virtual void detectAllRegions() = 0;                               // 所有区域各跑一轮(按各自到期判断)
    virtual void increaseFrequency(const std::string& regionId) = 0;   // 检测频率提升 -> 高频
    virtual void decreaseFrequency(const std::string& regionId) = 0;   // 检测频率降低 -> 低频

    virtual std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) = 0;
    virtual void consumeAlarms(const std::string& cameraId) = 0;

    // 生命周期: 初始化数据库/清理过期数据(模型在构造时经工厂加载)
    virtual void startDetection() = 0;
    virtual void stopDetection() = 0;
};

class DetectionService : public IDetectionService {
public:
    // 构造: 区域-模型对、模型定义、检测频率全部来自配置文件(ConfigManager::instance().ai())
    DetectionService(IModuleDao& dao,
                     RealTimeFrameCache& frameCache,
                     DetectionResultCache& resultCache,
                     const washcar::cfg::AiConfig& aiConfig);
    ~DetectionService() override;

    DetectionResult detectRegion(const std::string& regionId, bool force = false) override;
    void detectAllRegions() override;
    void increaseFrequency(const std::string& regionId) override;
    void decreaseFrequency(const std::string& regionId) override;

    std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) override;
    void consumeAlarms(const std::string& cameraId) override;

    void startDetection() override;
    void stopDetection() override;

private:
    // 一轮结果: 业务判定 + 写结果缓冲 + 变化才落库/留证
    void commitRound(const std::shared_ptr<DetectionRegion>& region,
                     const CameraRealTimeFrame& frame, int64_t callTime,
                     std::vector<DetectionResult::Item> items);
    std::string saveEventImage(const CameraRealTimeFrame& frame, const std::string& regionId,
                               int64_t seq, int64_t ts);   // 事件留证: 存完整原图 JPEG
    void cleanupAlarmImages();       // 异常图片目录: 超天数/超总量就删最旧的
    static int64_t nowMs();

private:
    IModuleDao& m_dao;
    RealTimeFrameCache& m_frameCache;
    DetectionResultCache& m_resultCache;

    int m_idleIntervalMs;
    int m_workingIntervalMs;
    int m_exitWorkingEmptyTimes;

    // 结果与异常图片的保存上限(现场磁盘有限)
    int m_resultRetentionDays;     // 检测结果保留天数
    int m_maxDetectionResults;     // 检测结果条数上限
    int m_maxAlarmImageMb;         // 异常图片目录总量上限(MB)
    int m_alarmImageKeepDays;      // 异常图片保留天数
    int m_logSummaryIntervalSec;   // 汇总日志基准间隔(秒)
    int m_logSummaryMaxIntervalSec;// 退避上限(秒)
    int64_t m_lastSummaryTime = 0; // 上次汇总时间
    int64_t m_summaryIntervalMs = 0;  // 当前汇总间隔(没人来时指数退避)
    int m_emptyRounds = 0;        // 连续多少轮什么都没检测到
    int m_cleanupCounter = 0;       // 图片清理节流计数(每 30 轮清一次)

    // 上一轮命中情况: 只有变化了才算"有事发生", 用来挡日志/落库刷屏
    std::string m_lastHitTypes;    // 命中项类型集合(car;person;)
    std::string m_lastHitValues;   // 命中项的值(车牌号等)

    std::unordered_map<std::string, std::shared_ptr<DetectionRegion>> m_regions;
    mutable std::mutex m_mutex;   // 保护 m_regions / 命中摘要

    std::string m_alarmImageDir = "./alarm";   // 异常图片落盘目录
    std::atomic<bool> m_running{false};
    std::atomic<int64_t> m_seq{ 0 };           // 检测结果序号自增
};
