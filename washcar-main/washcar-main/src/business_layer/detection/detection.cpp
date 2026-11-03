#include "business_layer/detection/detection.h"
#include "common/ai/ai_model_factory.h"
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
    m_alarmImageDir = ConfigManager::instance().imagesDir();
    if (!aiConfig.aiEnabled) {
        LOG_INFO("[Detection] AI 未启用(aiEnabled=false), 空转");
        return;
    }

    // 1. 模型定义查重: 同一个 modelId 只能定义一次(跨区域共享由工厂负责)
    {
        std::unordered_map<std::string, int> seen;
        for (const auto& model : aiConfig.modelConfigs) {
            if (++seen[model.modelId] > 1) {
                throw std::runtime_error("[Detection] duplicated modelId: " + model.modelId);
            }
        }
    }

    // 2. 区域: 各自把模型从工厂取出来, 同一 modelId 跨区域拿到同一个实例
    for (const auto& binding : aiConfig.regions) {
        if (m_regions.count(binding.regionId)) {
            throw std::runtime_error("[Detection] duplicated regionId: " + binding.regionId);
        }
        auto region = std::make_shared<DetectionRegion>(binding);
        std::string error;
        if (!region->bindModels(aiConfig.modelConfigs, error)) {
            throw std::runtime_error("[Detection] " + error);
        }
        m_regions[binding.regionId] = region;
    }
    LOG_INFO("[Detection] 区域就绪 regions=" + std::to_string(m_regions.size())
             + ", models=" + std::to_string(washcar::ai::AiModelFactory::instance().loadedCount()));
}

DetectionService::~DetectionService() {
    stopDetection();
    washcar::ai::AiModelFactory::instance().releaseExpired();
}

DetectionResult DetectionService::detectRegion(const std::string& regionId, bool force) {
    DetectionResult emptyResult;
    if (!m_running.load()) {
        return emptyResult;
    }

    std::shared_ptr<DetectionRegion> region;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_regions.find(regionId);
        if (it == m_regions.end()) {
            LOG_WARNING("[Detection] detectRegion: 未注册区域 regionId=" + regionId);
            return emptyResult;
        }
        region = it->second;
    }

    const int64_t now = nowMs();
    if (!force && !region->due(now, m_idleIntervalMs, m_workingIntervalMs)) {
        auto latest = m_resultCache.read(regionId);
        return latest.seq > 0 ? latest.result : emptyResult;
    }
    region->markDetected(now);

    CameraRealTimeFrame frame = m_frameCache.getCameraRealTimeFrame(CameraInfo(regionId));
    if (frame.getWidth() <= 0 || frame.getHeight() <= 0) {
        LOG_DEBUG("[Detection] region=" + regionId + " 没有可用帧, 跳过");
        return emptyResult;
    }

    const int64_t callTime = nowMs();
    // 同步: 顺序跑完本区域全部模型(NPU 串行), 拿到的是"这一轮全部结果"
    auto items = region->runModels(frame, frame.getTimestamp());
    commitRound(region, frame, callTime, std::move(items));

    auto record = m_resultCache.read(regionId);
    return record.result;
}

void DetectionService::detectAllRegions() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ids.reserve(m_regions.size());
        for (const auto& entry : m_regions) ids.push_back(entry.first);
    }
    for (const auto& id : ids) {
        detectRegion(id);
    }
}

void DetectionService::increaseFrequency(const std::string& regionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_regions.find(regionId);
    if (it == m_regions.end()) {
        LOG_WARNING("[Detection] increaseFrequency: 未注册区域 regionId=" + regionId);
        return;
    }
    it->second->enterWorking();
    LOG_INFO("[Detection] increaseFrequency: region=" + regionId + " -> 高频");
}

void DetectionService::decreaseFrequency(const std::string& regionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_regions.find(regionId);
    if (it == m_regions.end()) {
        LOG_WARNING("[Detection] decreaseFrequency: 未注册区域 regionId=" + regionId);
        return;
    }
    it->second->enterIdle();
    LOG_INFO("[Detection] decreaseFrequency: region=" + regionId + " -> 低频");
}

void DetectionService::startDetection() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;

    m_dao.initAIDao();
    m_dao.cleanupExpiredResults(m_resultRetentionDays);
    m_dao.cleanupOverflowResults(m_maxDetectionResults);
    cleanupAlarmImages();   // 开机清一次过期/超量的异常图片(跟日志同理)

    LOG_INFO("[Detection] startDetection: regions=" + std::to_string(m_regions.size())
             + ", 同步检测(无模型线程)");
}

void DetectionService::stopDetection() {
    if (!m_running.exchange(false)) return;
    LOG_INFO("[Detection] stopDetection");
}

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
void DetectionService::commitRound(const std::shared_ptr<DetectionRegion>& region,
                                   const CameraRealTimeFrame& frame, int64_t callTime,
                                   std::vector<DetectionResult::Item> items) {
    const std::string regionId = region->id();

    DetectionRecord record;
    record.seq = ++m_seq;
    record.captureTime = frame.getTimestamp();
    record.cameraId = regionId;                    // regionId == cameraId
    // 图像数据是需求的必备字段, 但只存引用不拷像素(帧缓冲本来就是 shared_ptr 持有)
    record.imageData.data = frame.getPixelData();
    record.imageData.width = frame.getWidth();
    record.imageData.height = frame.getHeight();
    record.modelId = region->modelIds();
    record.callTime = callTime;
    record.inferTime = nowMs();
    record.result.frameTimestamp = frame.getTimestamp();
    record.result.items = std::move(items);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 业务判定(升降频/去重/待报项)在区域内部, 区域状态受这把锁保护
        region->evaluate(record.result.items, record.captureTime, m_exitWorkingEmptyTimes,
                         record.result.alarms);
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
        std::string imgPath = saveEventImage(frame, regionId, record.seq, record.inferTime);
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
