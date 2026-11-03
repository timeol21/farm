#include "business_layer/detection/detection_region.h"

#include "common/ai/ai_model_factory.h"
#include "common/log/log_manager.h"

#include <algorithm>
#include <stdexcept>

using namespace washcar::cfg;

namespace {

const AiModelConfig* findModel(const std::vector<AiModelConfig>& models, const std::string& id) {
    for (const auto& model : models) {
        if (model.modelId == id) return &model;
    }
    return nullptr;
}

std::map<int, DetectionResult::Type> buildClassMap(const AiModelConfig& cfg) {
    std::map<int, DetectionResult::Type> classMap;
    const std::size_t count = std::min(cfg.classIds.size(), cfg.classTypes.size());
    for (std::size_t i = 0; i < count; ++i) {
        DetectionResult::Type type;
        if (!typeFromString(cfg.classTypes[i], type)) {
            throw std::runtime_error("[DetectionRegion] unknown classType: " + cfg.classTypes[i]
                                     + ", modelId=" + cfg.modelId);
        }
        classMap[cfg.classIds[i]] = type;
    }
    return classMap;
}

}  // namespace

DetectionRegion::DetectionRegion(const AiRegionBinding& binding) : m_binding(binding) {}

bool DetectionRegion::bindModels(const std::vector<AiModelConfig>& models, std::string& error) {
    auto bind = [&](const std::vector<std::string>& ids, std::vector<ModelBinding>& out) {
        for (const auto& id : ids) {
            const AiModelConfig* cfg = findModel(models, id);
            if (cfg == nullptr) {
                error = "region " + m_binding.regionId + " references undefined model: " + id;
                return false;
            }
            ModelBinding binding;
            binding.cfg = *cfg;
            try {
                binding.classMap = buildClassMap(*cfg);
                // 同一个 modelId 不管几个区域要, 拿到的都是同一个实例
                binding.model = washcar::ai::AiModelFactory::instance().acquire(*cfg);
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
            out.push_back(std::move(binding));
        }
        return true;
    };
    if (!bind(m_binding.idleModelIds, m_idleModels)) return false;
    if (!bind(m_binding.workingModelIds, m_workingModels)) return false;
    return true;
}

const std::vector<DetectionRegion::ModelBinding>* DetectionRegion::activeModels() const {
    return m_workingMode ? &m_workingModels : &m_idleModels;
}

std::string DetectionRegion::modelIds() const {
    std::string joined;
    for (const auto& binding : *activeModels()) {
        if (!joined.empty()) joined += ",";
        joined += binding.cfg.modelId;
    }
    return joined;
}

std::vector<DetectionResult::Item> DetectionRegion::runModels(const CameraRealTimeFrame& frame,
                                                              int64_t ts) const {
    std::vector<DetectionResult::Item> items;

    std::vector<uint8_t> rgb = frame.getBmpData();
    if (rgb.empty()) {
        LOG_WARNING("[DetectionRegion] " + m_binding.regionId + " 帧为空, 本轮不跑");
        return items;
    }

    washcar::ai::AiImage image;
    image.rgb = rgb.data();
    image.width = frame.getWidth();
    image.height = frame.getHeight();

    for (const auto& binding : *activeModels()) {
        // 占位项: 每个类别先给 detected=false, 命中再覆盖
        for (const auto& entry : binding.classMap) {
            items.push_back({entry.second, false, "", 0.0f, ts, ""});
        }
        if (!binding.model) continue;

        std::vector<washcar::ai::AiModelHit> hits = binding.model->run(image);
        for (const auto& hit : hits) {
            if (!hit.detected) continue;
            const auto type = binding.classMap.find(hit.classId);
            if (type == binding.classMap.end()) continue;
            for (auto& item : items) {
                if (item.type != type->second) continue;
                item.detected = true;
                item.confidence = hit.confidence;
                item.value = hit.value;
                if (!hit.imagePath.empty()) item.imagePath = hit.imagePath;
                for (int k = 0; k < 4; ++k) item.box[k] = hit.box[k];
            }
        }
    }
    return items;
}

void DetectionRegion::enterWorking() {
    m_workingMode = true;
    m_consecutiveEmpty = 0;
}

void DetectionRegion::enterIdle() {
    m_workingMode = false;
    m_consecutiveEmpty = 0;
}

bool DetectionRegion::due(int64_t nowMs, int idleMs, int workingMs) const {
    if (m_lastDetectionTime == 0) return true;
    return nowMs - m_lastDetectionTime >= intervalMs(idleMs, workingMs);
}

void DetectionRegion::evaluate(const std::vector<DetectionResult::Item>& items, int64_t ts,
                               int exitWorkingEmptyTimes, std::vector<DetectionAlarm>& alarms) {
    bool person = false;
    bool car = false;
    std::vector<DetectionResult::Type> abnormalTypes;
    for (const auto& item : items) {
        if (!item.detected) continue;
        switch (item.type) {
            case DetectionResult::Type::PERSON_DETECTED: person = true; break;
            case DetectionResult::Type::CAR_PRESENT:     car = true; break;
            case DetectionResult::Type::ROOF_OBSTACLE:
            case DetectionResult::Type::DOOR_OPEN:
            case DetectionResult::Type::WINDOW_OPEN:     abnormalTypes.push_back(item.type); break;
            default: break;
        }
    }

    if (m_workingMode) {
        if (person) {
            if (!m_personReported) {
                alarms.push_back({DetectionAlarm::Type::PERSON,
                                  DetectionResult::Type::PERSON_DETECTED, "", ts});
                m_personReported = true;
            }
            m_consecutiveEmpty = 0;
        } else {
            m_personReported = false;
            if (car) {
                m_consecutiveEmpty = 0;
            } else {
                m_consecutiveEmpty++;
            }
        }

        for (auto type : abnormalTypes) {
            if (!m_abnormalState[type]) {
                m_abnormalState[type] = true;
                std::string imgPath;
                for (const auto& item : items) {
                    if (item.type == type) { imgPath = item.imagePath; break; }
                }
                alarms.push_back({DetectionAlarm::Type::ABNORMAL, type, imgPath, ts});
            }
        }
        for (auto& entry : m_abnormalState) {
            const bool still = std::find(abnormalTypes.begin(), abnormalTypes.end(), entry.first)
                               != abnormalTypes.end();
            if (entry.second && !still) entry.second = false;
        }

        if (m_consecutiveEmpty >= exitWorkingEmptyTimes) {
            LOG_INFO("[Detection] region=" + m_binding.regionId + " 连续 "
                     + std::to_string(m_consecutiveEmpty) + " 次无人无车, 回低频");
            enterIdle();
        }
    } else {
        if (person) {
            if (!m_personReported) {
                alarms.push_back({DetectionAlarm::Type::PERSON,
                                  DetectionResult::Type::PERSON_DETECTED, "", ts});
                m_personReported = true;
            }
            enterWorking();
            LOG_INFO("[Detection] region=" + m_binding.regionId + " 检测到人, 升频");
        } else {
            m_personReported = false;
            if (car) {
                enterWorking();
                LOG_INFO("[Detection] region=" + m_binding.regionId + " 检测到车, 升频");
            }
        }
    }
}
