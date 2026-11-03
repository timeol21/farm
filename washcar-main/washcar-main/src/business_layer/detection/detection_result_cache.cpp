#include "business_layer/detection/detection_result_cache.h"

void DetectionResultCache::write(const std::string& regionId, const DetectionRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_[regionId] = record;   // 同源新结果覆盖旧结果
}

DetectionRecord DetectionResultCache::read(const std::string& regionId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(regionId);
    if (it != records_.end()) {
        return it->second;
    }
    return DetectionRecord();   // 未检测过: seq == 0 表示无效
}

void DetectionResultCache::consumeAlarms(const std::string& regionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(regionId);
    if (it != records_.end()) {
        it->second.result.alarms.clear();   // 已上报的待报项清掉, 下一轮重新判定
    }
}

void DetectionResultCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}
