#pragma once
#include "data_layer/ai/ai_dao.h"
#include "business_layer/detection/detection_object.h"
#include "business_layer/detection/detection_result_cache.h"
#include "business_layer/device/real_time_frame_cache.h"
#include "common/config/config_object.h"
#include "common/ai/yolov8/yolov8.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// 检测服务(业务类): 区域-模型表 + 每模型一线程 + 业务判定(频率/去重/待报项)全在内部。
// 构造注入"区域-模型对、模型定义、检测频率"(从配置文件读取), 构造内加载全部模型, 失败抛异常。
// 对外: detectRegion / detectAllRegions / increaseFrequency / decreaseFrequency;
// 旧接口(大厅上一轮仍调用)空实现占位, 下一轮大厅接入新接口。
class IDetectionService {
public:
    virtual ~IDetectionService() = default;

    // ---- 新接口(重点) ----
    virtual void detectRegion(const std::string& regionId) = 0;        // 指定区域检测(异步, 内部按模式判到期)
    virtual void detectAllRegions() = 0;                               // 所有已注册区域检测(异步)
    virtual void increaseFrequency(const std::string& regionId) = 0;   // 检测频率提升 -> 高频
    virtual void decreaseFrequency(const std::string& regionId) = 0;   // 检测频率降低 -> 低频

    // ---- 旧接口(历史遗留; 大厅侧读结果/取告警走这两个) ----
    virtual std::vector<std::string> listRegions() const { return {}; }
    virtual std::vector<DetectionRegion> listRegionConfigs() const { return {}; }
    virtual void detectBufferedFrames() {}
    virtual void detectFrame(const std::string& cameraId, const FrameData& frame) {}
    virtual void setWorkingMode(const std::string& cameraId) {}
    virtual void setIdleMode(const std::string& cameraId) {}
    virtual std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) { return nullptr; }
    virtual void consumeAlarms(const std::string& cameraId) {}

    // ---- 生命周期(模型加载/卸载在构造/析构, 无对外注册方法) ----
    virtual void startDetection() = 0;
    virtual void stopDetection() = 0;
};

class DetectionService : public IDetectionService {
public:
    // 构造: 区域-模型对、模型定义、检测频率全部来自配置文件(传入 ConfigManager::instance().ai()):
    //       构造内解析模型定义(字符串类别名 -> 枚举)并加载全部模型, 任一失败 throw std::runtime_error
    DetectionService(IModuleDao& dao,
                     RealTimeFrameCache& frameCache,
                     DetectionResultCache& resultCache,
                     const washcar::cfg::AiConfig& aiConfig);
    ~DetectionService();   // 析构: 停线程 + 卸载全部模型

    void detectRegion(const std::string& regionId) override;
    void detectAllRegions() override;
    void increaseFrequency(const std::string& regionId) override;
    void decreaseFrequency(const std::string& regionId) override;

    // 大厅侧读检测结果 / 取走待报项
    std::shared_ptr<DetectionResult> getLatestDetectionResult(const std::string& cameraId) override;
    void consumeAlarms(const std::string& cameraId) override;

    void startDetection() override;
    void stopDetection() override;

private:
    struct ModelInstance;   // 单模型: 配置(classMap 解析后) + rknn ctx + worker 线程(任务队列 + cv)
    struct RegionRuntime;   // 单区域: 绑定 + 模型引用 + 业务状态(模式/空转/去重) + 轮次合并槽
    struct PendingRound;    // 一次检测的合并槽(同轮多模型结果合并后提交)
    struct ModelTask;       // 派发给模型线程的一次检测任务

    void dispatchRegion(const std::shared_ptr<RegionRuntime>& region,
                        const CameraRealTimeFrame& frame, int64_t callTime);  // 建合并槽 + 唤醒模型线程
    void modelWorker(const std::shared_ptr<ModelInstance>& inst);             // 模型线程主循环(cv 休眠)
    std::vector<DetectionResult::Item> runModel(const std::shared_ptr<ModelInstance>& model,
                                                const CameraRealTimeFrame& frame, int64_t ts);
    void commitRound(const std::string& regionId, const std::shared_ptr<PendingRound>& round,
                     std::vector<DetectionResult::Item> items);  // 业务判定 + 写结果缓冲 + 落库
    void evaluateRegion(const std::shared_ptr<RegionRuntime>& region, int64_t ts,
                        const std::vector<DetectionResult::Item>& items,
                        std::vector<DetectionAlarm>& alarms);    // 升降频/去重/待报项(调用方持锁)
    std::string saveEventImage(const CameraRealTimeFrame& frame, const std::string& regionId,
                               int64_t seq, int64_t ts);   // 事件留证: 存完整原图 JPEG
    void cleanupAlarmImages();       // 异常图片目录: 超天数/超总量就删最旧的
    void loadModel(const washcar::cfg::AiModelConfig& cfg);      // 构造内调用, 失败抛异常
    static int64_t nowMs();

private:
    IModuleDao& m_dao;
    RealTimeFrameCache& m_frameCache;
    DetectionResultCache& m_resultCache;

    // 检测频率(私有成员, 从配置来, 不封装)
    int m_idleIntervalMs;
    int m_workingIntervalMs;
    int m_exitWorkingEmptyTimes;

    // 检测结果与异常图片的保存上限(现场磁盘有限, 跟日志一样要有封顶)
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

    // 上一轮的命中情况: 只有变化了才算"有事发生", 用来挡日志/落库刷屏
    std::string m_lastHitTypes;    // 命中项类型集合(car;person;)
    std::string m_lastHitValues;   // 命中项的值(车牌号等)

    std::unordered_map<std::string, std::shared_ptr<ModelInstance>> m_models;
    std::unordered_map<std::string, std::shared_ptr<RegionRuntime>> m_regions;
    mutable std::mutex m_mutex;   // 保护 m_models / m_regions / 业务状态 / 轮次合并槽

    std::string m_alarmImageDir = "./alarm";   // 异常图片落盘目录
    std::atomic<bool> m_running{ false };
    std::atomic<int64_t> m_seq{ 0 };           // 检测结果序号自增
};
