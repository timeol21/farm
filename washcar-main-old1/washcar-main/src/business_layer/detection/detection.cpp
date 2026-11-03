#include "business_layer/detection/detection.h"
#include "common/ai/ai_model.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "rknn_api.h"
#include "file_utils.h"
#include "image_utils.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <vector>

// 业务类: 区域-模型表 + 每模型一线程(cv 休眠) + 业务判定全在内部。
// 构造从配置文件(aiConfig)加载全部模型并构建区域表, 任一失败抛异常; 析构卸载。
// 大厅定时器调 detectRegion / detectAllRegions 驱动(内部按模式判到期),
// 检测结果写 DetectionResultCache(按区域, 同源新结果覆盖旧结果), 与"报不报警"解耦:
//   低频 {车,人}: 有人->语音待报+升频; 有车->升频
//   高频 {人,车顶,车门,车窗,车牌}: 有人->语音待报; 异常(状态变化)->告警待报+图片
//   连续 exitWorkingEmptyTimes 次无人无车 -> 回低频

// ============ 内部结构定义 ============
struct DetectionService::ModelInstance {
    washcar::cfg::AiModelConfig cfg;                 // 配置(含 classIds/classTypes)
    std::map<int, DetectionResult::Type> classMap;   // class_id -> 业务 Type(构造时解析)
    std::shared_ptr<washcar::ai::IAiModel> model;    // 模型实现(按 modelKind 决定具体类型)

    std::thread          thread;                     // 本模型 worker 线程
    std::mutex           taskMutex;                  // 保护 pendingTasks
    std::condition_variable cv;                      // 派发 -> notify 唤醒
    std::vector<ModelTask> pendingTasks;
};

struct DetectionService::PendingRound {
    int64_t                  frameTimestamp = 0;     // 帧采集时间
    int64_t                  callTime = 0;           // 调用时间(派发时刻)
    CameraRealTimeFrame      frame;                  // 本轮快照帧(同轮所有模型用同一帧)
    std::string              modelIds;               // 本轮全部模型 id(逗号拼接)
    int                      remainingModels = 0;    // 未完成模型数, 归零 = 全部跑完可提交
    std::vector<DetectionResult::Item> items;        // 已收集的命中项
};

struct DetectionService::RegionRuntime {
    washcar::cfg::AiRegionBinding binding;           // 区域绑定(regionId == cameraId)
    std::vector<std::shared_ptr<ModelInstance>> idleModels;     // 低频模型子集(解析后)
    std::vector<std::shared_ptr<ModelInstance>> workingModels;  // 高频模型子集(解析后)

    // 业务状态(频率由检测结果驱动)
    bool workingMode = false;                        // true=高频 / false=低频
    int64_t lastDetectionTime = 0;                   // 上次实际派发时间(决定是否到期)
    int consecutiveEmpty = 0;                        // 高频下连续"无人无车"次数
    bool personReported = false;                     // 人已上报(持续在场不重复报, 消失后复位)
    std::map<DetectionResult::Type, bool> abnormalState;   // 异常去重: 状态变化才上报

    std::shared_ptr<PendingRound> pendingRound;      // 进行中的轮次合并槽(m_mutex 保护)
};

struct DetectionService::ModelTask {
    std::string regionId;                            // 目标区域
    CameraRealTimeFrame frame;                       // 本轮快照帧
    int64_t ts = 0;                                  // 帧采集时间
    std::shared_ptr<PendingRound> round;             // 所属轮次合并槽
};

int64_t DetectionService::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ============ 构造/析构(模型加载/卸载) ============
DetectionService::DetectionService(IModuleDao& dao, RealTimeFrameCache& frameCache,
                                   DetectionResultCache& resultCache,
                                   const washcar::cfg::AiConfig& aiConfig)
    : m_dao(dao), m_frameCache(frameCache), m_resultCache(resultCache),
      m_idleIntervalMs(aiConfig.idleIntervalMs),
      m_workingIntervalMs(aiConfig.workingIntervalMs),
      m_exitWorkingEmptyTimes(aiConfig.exitWorkingEmptyTimes),
      m_resultRetentionDays(aiConfig.resultRetentionDays),
      m_maxDetectionResults(aiConfig.maxDetectionResults),
      m_maxAlarmImageMb(aiConfig.maxAlarmImageMb),
      m_alarmImageKeepDays(aiConfig.alarmImageKeepDays),
      m_logSummaryIntervalSec(aiConfig.logSummaryIntervalSec),
      m_logSummaryMaxIntervalSec(aiConfig.logSummaryMaxIntervalSec) {
    // 告警图片跟着持久化根目录走,不再硬编码 ./alarm。
    // 目录口径取自 ConfigManager,和 main 装配是同一处定义;空串 = 没有可用持久化根,不落盘。
    m_alarmImageDir = ConfigManager::instance().imagesDir();
    try {
        if (!aiConfig.aiEnabled) {
            // AI 关闭(主开关, 同 plcEnabled): 不加载模型、不建区域表, 后续 detect/升降频对未注册区域静默告警
            LOG_INFO("[Detection] AI 未启用(aiEnabled=false), 空转");
            return;
        }
        // 1. 加载全部模型(任一失败抛异常, 构造即失败退出)
        for (const auto& mc : aiConfig.modelConfigs) {
            if (m_models.count(mc.modelId)) {
                throw std::runtime_error("[Detection] duplicated modelId: " + mc.modelId);
            }
            loadModel(mc);
        }
        // 2. 构建区域表(regionId == cameraId, 帧缓冲按它取帧)
        for (const auto& rb : aiConfig.regions) {
            if (m_regions.count(rb.regionId)) {
                throw std::runtime_error("[Detection] duplicated regionId: " + rb.regionId);
            }
            auto rt = std::make_shared<RegionRuntime>();
            rt->binding = rb;
            auto bindModels = [&](const std::vector<std::string>& ids,
                                  std::vector<std::shared_ptr<ModelInstance>>& out) {
                for (const auto& id : ids) {
                    auto it = m_models.find(id);
                    if (it == m_models.end()) {
                        throw std::runtime_error("[Detection] region " + rb.regionId
                                                 + " references undefined model: " + id);
                    }
                    out.push_back(it->second);
                }
            };
            bindModels(rb.idleModelIds, rt->idleModels);
            bindModels(rb.workingModelIds, rt->workingModels);
            m_regions[rb.regionId] = rt;
        }
    } catch (...) {
        // 构造失败: 析构不会被调用, 手动释放已加载的模型
        for (auto& kv : m_models) {
            kv.second->model.reset();
        }
        throw;
    }
}

DetectionService::~DetectionService() {
    stopDetection();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& kv : m_models) {
        kv.second->model.reset();
    }
}

// ============ 模型加载(构造内调用) ============
void DetectionService::loadModel(const washcar::cfg::AiModelConfig& cfg) {
    auto inst = std::make_shared<ModelInstance>();
    inst->cfg = cfg;

    // 类别名 -> 业务枚举(无效名 = 配置错误)
    size_t n = std::min(cfg.classIds.size(), cfg.classTypes.size());
    for (size_t i = 0; i < n; ++i) {
        DetectionResult::Type t;
        if (!typeFromString(cfg.classTypes[i], t)) {
            throw std::runtime_error("[Detection] invalid classType '" + cfg.classTypes[i]
                                     + "' in model " + cfg.modelId);
        }
        inst->classMap[cfg.classIds[i]] = t;
    }
    if (cfg.modelPaths.empty()) {
        // 部署问题(模型文件没放)不是配置错误: 记日志跳过这个模型, 不拖垮主程序
        LOG_ERROR("[Detection] modelPaths empty, 模型跳过: " + cfg.modelId);
        m_models[cfg.modelId] = inst;   // 占位: 区域引用它不会报"未定义模型"
        return;
    }

    // 按 modelKind 造对应模型(通用检测 / 车牌两级串联), 差异收在子类里
    auto model = washcar::ai::createAiModel(cfg.modelKind);
    if (!model) {
        throw std::runtime_error("[Detection] unknown modelKind '" + cfg.modelKind
                                 + "' for modelId: " + cfg.modelId);
    }
    if (!model->load(cfg)) {
        // 模型文件缺失/损坏: 同样只跳过这个模型, 主程序继续跑
        LOG_ERROR("[Detection] 模型加载失败, 该模型不参与检测: modelId=" + cfg.modelId);
        m_models[cfg.modelId] = inst;
        return;
    }

    inst->model = std::move(model);
    m_models[cfg.modelId] = inst;
    std::string paths;
    for (const auto& p : cfg.modelPaths) {
        if (!paths.empty()) paths += ",";
        paths += p;
    }
    LOG_INFO(std::string("[Detection] model loaded: ") + cfg.modelId
             + ", kind=" + cfg.modelKind
             + ", path=" + paths + ", classes=" + std::to_string(inst->classMap.size()));
}

// ============ 一次性检测接口(大厅定时器驱动) ============
void DetectionService::detectRegion(const std::string& regionId) {
    int64_t now = nowMs();
    std::shared_ptr<RegionRuntime> rt;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        auto it = m_regions.find(regionId);
        if (it == m_regions.end()) {
            LOG_WARNING("[Detection] detectRegion: 未注册区域 regionId=" + regionId);
            return;
        }
        rt = it->second;
        // 按当前模式判到期, 未到期跳过(大厅定时器只管驱动, 检测节奏由服务内部控制)
        int interval = rt->workingMode ? m_workingIntervalMs : m_idleIntervalMs;
        if (now - rt->lastDetectionTime < interval) return;
        rt->lastDetectionTime = now;
    }
    // 帧缓冲(RealTimeFrameCache)按区域 id(=摄像头 id)取帧, 无有效帧本轮跳过
    CameraRealTimeFrame frame = m_frameCache.getCameraRealTimeFrame(CameraInfo(regionId));
    if (!frame.isValid()) return;
    dispatchRegion(rt, frame, now);
}

void DetectionService::detectAllRegions() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ids.reserve(m_regions.size());
        for (const auto& kv : m_regions) {
            ids.push_back(kv.first);
        }
    }
    for (const auto& id : ids) {
        detectRegion(id);
    }
}

// ============ 频率提升/降低(对外, 供大厅按业务切换) ============
void DetectionService::increaseFrequency(const std::string& regionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_regions.find(regionId);
    if (it == m_regions.end()) {
        LOG_WARNING("[Detection] increaseFrequency: 未注册区域 regionId=" + regionId);
        return;
    }
    it->second->workingMode = true;
    it->second->consecutiveEmpty = 0;
    LOG_INFO("[Detection] increaseFrequency: region=" + regionId + " -> 高频");
}

void DetectionService::decreaseFrequency(const std::string& regionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_regions.find(regionId);
    if (it == m_regions.end()) {
        LOG_WARNING("[Detection] decreaseFrequency: 未注册区域 regionId=" + regionId);
        return;
    }
    it->second->workingMode = false;
    it->second->consecutiveEmpty = 0;
    LOG_INFO("[Detection] decreaseFrequency: region=" + regionId + " -> 低频");
}

// ============ 生命周期: 模型线程 ============
void DetectionService::startDetection() {
    std::vector<std::shared_ptr<ModelInstance>> insts;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running) return;
        m_running = true;
        m_dao.initAIDao();
        m_dao.cleanupExpiredResults(m_resultRetentionDays);
        m_dao.cleanupOverflowResults(m_maxDetectionResults);
        cleanupAlarmImages();   // 开机清一次过期/超量的异常图片(跟日志同理)
        for (auto& kv : m_models) {
            insts.push_back(kv.second);
        }
    }
    for (auto& inst : insts) {
        std::lock_guard<std::mutex> lock(inst->taskMutex);
        if (!inst->thread.joinable()) {
            inst->thread = std::thread(&DetectionService::modelWorker, this, inst);
        }
    }
    LOG_INFO(std::string("[Detection] startDetection: models=") + std::to_string(insts.size())
             + ", regions=" + std::to_string(m_regions.size()));
}

void DetectionService::stopDetection() {
    std::vector<std::shared_ptr<ModelInstance>> insts;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        m_running = false;
        for (auto& kv : m_models) {
            insts.push_back(kv.second);
        }
    }
    for (auto& inst : insts) {
        {
            std::lock_guard<std::mutex> lock(inst->taskMutex);
            inst->cv.notify_all();
        }
        if (inst->thread.joinable()) {
            inst->thread.join();
        }
    }
    LOG_INFO("[Detection] stopDetection: all model threads stopped");
}

void DetectionService::modelWorker(const std::shared_ptr<ModelInstance>& inst) {
    while (true) {
        ModelTask task;
        {
            std::unique_lock<std::mutex> lock(inst->taskMutex);
            // 平时休眠, 等 detectRegion 派发任务
            inst->cv.wait(lock, [&] {
                return !m_running || !inst->pendingTasks.empty();
            });
            if (!m_running) break;
            task = std::move(inst->pendingTasks.front());
            inst->pendingTasks.erase(inst->pendingTasks.begin());
        }

        // 推理
        auto items = runModel(inst, task.frame, task.ts);

        // 留证统一在 commitRound 里做: 存一张完整原图 + 框坐标, 异常项也指向同一张原图

        // 合并进轮次, 最后一个模型负责提交(业务判定 + 写缓冲 + 落库)
        bool lastOne = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_regions.find(task.regionId);
            if (it == m_regions.end()) continue;                    // 区域已注销, 丢弃
            if (it->second->pendingRound != task.round) continue;   // 旧轮被新轮覆盖, 作废
            auto round = task.round;
            for (auto& item : items) {
                round->items.push_back(item);
            }
            if (--round->remainingModels == 0) {
                it->second->pendingRound.reset();
                lastOne = true;
            }
        }
        if (lastOne) {
            commitRound(task.regionId, task.round, std::move(task.round->items));
        }
    }
}

// ============ 派发一轮(建合并槽 + 唤醒模型线程) ============
void DetectionService::dispatchRegion(const std::shared_ptr<RegionRuntime>& region,
                                      const CameraRealTimeFrame& frame, int64_t callTime) {
    std::vector<std::shared_ptr<ModelInstance>> models;
    auto round = std::make_shared<PendingRound>();
    round->frameTimestamp = frame.getTimestamp();
    round->callTime = callTime;
    round->frame = frame;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        models = region->workingMode ? region->workingModels : region->idleModels;
        if (models.empty()) {
            LOG_WARNING("[Detection] dispatchRegion: region=" + region->binding.regionId
                        + " 无可用模型");
            return;
        }
        round->remainingModels = static_cast<int>(models.size());
        for (const auto& m : models) {
            if (!round->modelIds.empty()) round->modelIds += ",";
            round->modelIds += m->cfg.modelId;
        }
        region->pendingRound = round;   // 旧轮未完成则作废(新轮覆盖)
    }
    for (const auto& m : models) {
        ModelTask task;
        task.regionId = region->binding.regionId;
        task.frame = frame;
        task.ts = round->frameTimestamp;
        task.round = round;
        {
            std::lock_guard<std::mutex> lock(m->taskMutex);
            m->pendingTasks.push_back(std::move(task));
        }
        m->cv.notify_one();
    }
}

// ============ 单模型推理 ============
std::vector<DetectionResult::Item> DetectionService::runModel(
    const std::shared_ptr<ModelInstance>& model, const CameraRealTimeFrame& frame, int64_t ts) {
    std::vector<DetectionResult::Item> items;
    // 占位项: classMap 每条目输出一个 Item(detected=false)
    for (const auto& kv : model->classMap) {
        items.push_back({kv.second, false, "", 0.0f, ts, ""});
    }

    if (!model->model) {
        return items;  // 模型未加载: 全 false
    }

    // 1. 拿 RGB 字节
    std::vector<uint8_t> rgb = frame.getBmpData();
    if (rgb.empty()) {
        LOG_WARNING("[Detection] runModel: getBmpData empty, modelId=" + model->cfg.modelId);
        return items;
    }

    // 2. 组装成模型看到的图像(不拷贝, 帧缓冲在本轮内有效)
    washcar::ai::AiImage image;
    image.rgb = rgb.data();
    image.width = frame.getWidth();
    image.height = frame.getHeight();

    // 3. 推理(模型种类不同, 各自实现)
    std::vector<washcar::ai::AiModelHit> hits = model->model->run(image);

    // 4. class_id -> 业务 Type 映射: 命中即 detected=true
    for (const auto& hit : hits) {
        if (!hit.detected) continue;   // 模型给了占位项(未检出)就不算命中
        auto it = model->classMap.find(hit.classId);
        if (it == model->classMap.end()) continue;
        for (auto& item : items) {
            if (item.type == it->second) {
                item.detected = true;
                item.confidence = hit.confidence;
                item.value = hit.value;      // 车牌号等文本结果
                if (!hit.imagePath.empty()) item.imagePath = hit.imagePath;   // 模型自存的留证图
                for (int k = 0; k < 4; ++k) item.box[k] = hit.box[k];         // 命中位置(原图坐标)
            }
        }
    }
    return items;
}

// ============ 检测结果读取(大厅侧用) ============
std::shared_ptr<DetectionResult> DetectionService::getLatestDetectionResult(const std::string& cameraId) {
    auto rec = m_resultCache.read(cameraId);
    if (rec.seq <= 0) {
        return nullptr;   // 还没检测过
    }
    return std::make_shared<DetectionResult>(rec.result);
}

void DetectionService::consumeAlarms(const std::string& cameraId) {
    m_resultCache.consumeAlarms(cameraId);
}

// ============ 提交一轮(业务判定 + 写结果缓冲 + 落库) ============
void DetectionService::commitRound(const std::string& regionId,
                                   const std::shared_ptr<PendingRound>& round,
                                   std::vector<DetectionResult::Item> items) {
    DetectionRecord record;
    record.seq = ++m_seq;
    record.captureTime = round->frameTimestamp;
    record.cameraId = regionId;                    // regionId == cameraId
    record.imageData = round->frame.getBmpData();
    record.modelId = round->modelIds;
    record.callTime = round->callTime;
    record.inferTime = nowMs();
    record.result.frameTimestamp = round->frameTimestamp;
    record.result.items = std::move(items);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_regions.find(regionId);
        if (it == m_regions.end()) return;
        evaluateRegion(it->second, round->frameTimestamp, record.result.items, record.result.alarms);
        m_resultCache.write(regionId, record);     // 结果缓冲按区域, 同源新结果覆盖旧结果
    }
    // 命中摘要: 类型集合(判状态变化) + 值(判内容变化)
    std::string sig, vals;
    for (const auto& it : record.result.items) {
        if (!it.detected) continue;
        sig += DetectionResult::itemTypeName(it.type) + ";";
        if (!it.value.empty()) {
            vals += DetectionResult::itemTypeName(it.type) + "=" + it.value + ";";
        }
    }
    bool typeChanged = false, valueChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        typeChanged = (sig != m_lastHitTypes);
        valueChanged = (vals != m_lastHitValues);
        m_lastHitTypes = sig;
        m_lastHitValues = vals;
    }

    // 日志只打"有事发生"的轮次。每轮都打的话 2 秒一条全是 person=0 car=0,
    // 一天 4.3 万条纯噪音。另配一条低频汇总, 用来分辨"没检出"和"检测服务没跑"。
    if (typeChanged || valueChanged) {
        std::string line = "[Detection] " + regionId + " 命中: " + (sig.empty() ? "(无)" : sig);
        if (!vals.empty()) line += " " + vals;
        line += " cost=" + std::to_string(record.inferTime - record.callTime) + "ms";
        if (!record.result.alarms.empty()) {
            line += " alarms=" + std::to_string(record.result.alarms.size());
        }
        LOG_INFO(line);
    }
    // 汇总日志: 没人来洗车时指数退避(60s→120s→240s…到上限), 一有检测结果就重新记时。
    // 长时间空转不该一直刷屏, 但也不能完全静默(要能看出服务还活着)。
    if (m_logSummaryIntervalSec > 0 && m_summaryIntervalMs <= 0) {
        m_summaryIntervalMs = (int64_t)m_logSummaryIntervalSec * 1000;
    }
    if (m_logSummaryIntervalSec > 0
            && record.inferTime - m_lastSummaryTime >= m_summaryIntervalMs) {
        m_lastSummaryTime = record.inferTime;
        if (sig.empty()) {
            m_emptyRounds++;
            int64_t next = (int64_t)m_logSummaryIntervalSec * 1000;
            for (int i = 0; i < m_emptyRounds; i++) {
                next *= 2;
                if (next >= (int64_t)m_logSummaryMaxIntervalSec * 1000) break;
            }
            m_summaryIntervalMs = std::min<int64_t>(next, (int64_t)m_logSummaryMaxIntervalSec * 1000);
        } else {
            m_emptyRounds = 0;
            m_summaryIntervalMs = (int64_t)m_logSummaryIntervalSec * 1000;   // 有检测结果 -> 重新记时
        }
        LOG_INFO("[Detection] 汇总 " + regionId + ": 最近命中=" + (sig.empty() ? "(无)" : sig)
                 + ", 累计 " + std::to_string(m_seq) + " 轮"
                 + ", 连续无检测 " + std::to_string(m_emptyRounds) + " 次"
                 + ", 下次间隔 " + std::to_string(m_summaryIntervalMs / 1000) + "s");
    }

    // 有事发生才落库 + 存一张完整原图:
    //   落库只存变化, 否则库里全是重复信息, 把真正有用的记录挤掉
    //   图片存完整原图(JPEG)不存裁剪图: 裁剪图缺上下文, 出问题复现不了;
    //   框坐标和原图一起存, 任意一条记录都能还原"哪张图的哪个位置检出了什么"
    if (typeChanged || valueChanged) {
        std::string imgPath = saveEventImage(round->frame, regionId, record.seq, record.inferTime);
        if (!imgPath.empty()) {
            for (auto& it : record.result.items) {
                if (it.detected) it.imagePath = imgPath;
            }
            m_resultCache.write(regionId, record);   // 缓存补上图片路径
        }
        m_dao.saveDetectionResult(record.cameraId, record.result);   // 落库
        // 保存上限: 天数 + 条数双封顶, 库文件大小才不会一直涨
        m_dao.cleanupExpiredResults(m_resultRetentionDays);
        m_dao.cleanupOverflowResults(m_maxDetectionResults);
    }
    // 异常图片目录按节流清(每 30 轮一次, 约 1 分钟)
    if (++m_cleanupCounter >= 30) {
        m_cleanupCounter = 0;
        cleanupAlarmImages();
    }
}

// ============ 业务判定: 升降频/去重/待报项(调用方持锁) ============
void DetectionService::evaluateRegion(const std::shared_ptr<RegionRuntime>& region, int64_t ts,
                                      const std::vector<DetectionResult::Item>& items,
                                      std::vector<DetectionAlarm>& alarms) {
    // 从本轮结果提取业务信号(车牌仅记录, 不参与判定)
    bool person = false, car = false;
    std::vector<DetectionResult::Type> abnormalTypes;
    for (const auto& it : items) {
        if (!it.detected) continue;
        switch (it.type) {
            case DetectionResult::Type::PERSON_DETECTED: person = true; break;
            case DetectionResult::Type::CAR_PRESENT:     car = true; break;
            case DetectionResult::Type::ROOF_OBSTACLE:
            case DetectionResult::Type::DOOR_OPEN:
            case DetectionResult::Type::WINDOW_OPEN:     abnormalTypes.push_back(it.type); break;
            default: break;
        }
    }

    if (region->workingMode) {
        // ---- 高频 ----
        if (person) {
            // 人: 出现才报(持续在场不刷屏), 语音待报项
            if (!region->personReported) {
                alarms.push_back({DetectionAlarm::Type::PERSON,
                                  DetectionResult::Type::PERSON_DETECTED, "", ts});
                region->personReported = true;
            }
            region->consecutiveEmpty = 0;
        } else {
            region->personReported = false;   // 人消失, 允许再次出现再报
            if (car) {
                region->consecutiveEmpty = 0;
            } else {
                region->consecutiveEmpty++;
            }
        }

        // 异常: 状态变化才报(去重), 带图片路径
        for (auto t : abnormalTypes) {
            if (!region->abnormalState[t]) {
                region->abnormalState[t] = true;
                std::string imgPath;
                for (const auto& it : items) {
                    if (it.type == t) { imgPath = it.imagePath; break; }
                }
                alarms.push_back({DetectionAlarm::Type::ABNORMAL, t, imgPath, ts});
            }
        }
        // 异常恢复: 重置去重标记
        for (auto& kv : region->abnormalState) {
            bool still = false;
            for (auto t : abnormalTypes) {
                if (t == kv.first) { still = true; break; }
            }
            if (kv.second && !still) {
                kv.second = false;
            }
        }

        // 退出高频: 一段时间无人无车
        if (region->consecutiveEmpty >= m_exitWorkingEmptyTimes) {
            LOG_INFO("[Detection] region=" + region->binding.regionId + " 连续 "
                + std::to_string(region->consecutiveEmpty) + " 次无人无车, 回低频");
            region->workingMode = false;
            region->consecutiveEmpty = 0;
        }
    } else {
        // ---- 低频: 有人(语音+升频) / 有车(升频) ----
        if (person) {
            if (!region->personReported) {
                alarms.push_back({DetectionAlarm::Type::PERSON,
                                  DetectionResult::Type::PERSON_DETECTED, "", ts});
                region->personReported = true;
            }
            region->workingMode = true;
            LOG_INFO("[Detection] region=" + region->binding.regionId + " 检测到人, 升频");
        } else {
            region->personReported = false;
            if (car) {
                region->workingMode = true;
                LOG_INFO("[Detection] region=" + region->binding.regionId + " 检测到车, 升频");
            }
        }
    }
}

// ============ 图片落盘 ============
// ============ 事件留证: 存完整原图 ============
// 存原图不存裁剪图: 裁剪图丢上下文, 识别错了没法复现到底是检测错还是识别错。
// 原图 JPEG(约 250KB)配记录里的框坐标, 才是完整证据链。
std::string DetectionService::saveEventImage(const CameraRealTimeFrame& frame,
                                             const std::string& regionId,
                                             int64_t seq, int64_t ts) {
    namespace fs = std::filesystem;
    if (m_alarmImageDir.empty()) {
        return "";
    }
    try {
        fs::create_directories(m_alarmImageDir);
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("[Detection] saveEventImage: create dir failed, ") + e.what());
        return "";
    }
    std::string fname = m_alarmImageDir + "/" + regionId + "_" + std::to_string(seq)
                      + "_" + std::to_string(ts) + ".jpg";
    std::vector<uint8_t> rgb = frame.getBmpData();
    if (rgb.empty()) {
        return "";
    }
    image_buffer_t img = {};
    img.width = frame.getWidth();
    img.height = frame.getHeight();
    img.width_stride = img.width;
    img.height_stride = img.height;
    img.format = IMAGE_FORMAT_RGB888;
    img.size = (int)rgb.size();
    img.virt_addr = rgb.data();
    img.fd = 0;
    if (write_image(fname.c_str(), &img) != 0) {
        LOG_ERROR("[Detection] saveEventImage: write_image failed, path=" + fname);
        return "";
    }
    return fname;
}

// ============ 异常图片目录清理(超天数/超总量就删最旧的) ============
void DetectionService::cleanupAlarmImages() {
    namespace fs = std::filesystem;
    if (m_alarmImageDir.empty() || m_maxAlarmImageMb <= 0) {
        return;
    }
    std::error_code ec;
    if (!fs::exists(m_alarmImageDir, ec)) {
        return;
    }

    struct ImgFile {
        fs::path path;
        int64_t size;
        fs::file_time_type::clock::time_point mtime;
    };
    std::vector<ImgFile> files;
    int64_t total = 0;
    for (const auto& e : fs::directory_iterator(m_alarmImageDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        int64_t sz = (int64_t)fs::file_size(e.path(), ec);
        files.push_back({e.path(), sz, fs::last_write_time(e.path(), ec)});
        total += sz;
    }
    if (files.empty()) {
        return;
    }

    // 1) 过期先删
    int deleted = 0;
    if (m_alarmImageKeepDays > 0) {
        auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * m_alarmImageKeepDays);
        for (const auto& f : files) {
            if (f.mtime < cutoff && fs::remove(f.path, ec)) {
                total -= f.size;
                deleted++;
            }
        }
    }
    // 2) 还超总量就按时间从旧到新删
    int64_t maxBytes = (int64_t)m_maxAlarmImageMb * 1024 * 1024;
    if (total > maxBytes) {
        std::sort(files.begin(), files.end(),
                  [](const ImgFile& a, const ImgFile& b) { return a.mtime < b.mtime; });
        for (const auto& f : files) {
            if (total <= maxBytes) break;
            if (fs::remove(f.path, ec)) {
                total -= f.size;
                deleted++;
            }
        }
    }
    if (deleted > 0) {
        LOG_INFO("[Detection] cleanupAlarmImages: removed " + std::to_string(deleted)
            + " files, now " + std::to_string(total / 1024) + "KB (limit "
            + std::to_string(m_maxAlarmImageMb) + "MB)");
    }
}
