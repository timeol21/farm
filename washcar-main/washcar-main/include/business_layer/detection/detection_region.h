#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "business_layer/detection/detection_object.h"
#include "common/ai/ai_model.h"
#include "common/config/config_object.h"
#include "data_layer/camera/camera_real_time_frame.h"

// 检测区域: 一个区域绑定多个模型(不同区域可以共用同一个模型实例, 所以用 shared_ptr),
// 对外只做两件事 —— 同步跑一轮拿结果、维护自己的频率/去重状态。
class DetectionRegion {
public:
    explicit DetectionRegion(const washcar::cfg::AiRegionBinding& binding);
    ~DetectionRegion() = default;

    // ===== 初始化: 按配置把模型从工厂取出来(同一 modelId 跨区域共享)。失败填 error 返回 false。 =====
    bool bindModels(const std::vector<washcar::cfg::AiModelConfig>& models, std::string& error);

    // ===== 执行: 同步跑完当前模式下的全部模型。NPU 只能串行, 这里就是顺序调 run()。 =====
    std::vector<DetectionResult::Item> runModels(const CameraRealTimeFrame& frame, int64_t ts) const;

    // ===== 调度: 该不该跑、用什么频率 =====
    bool due(int64_t nowMs, int idleMs, int workingMs) const;
    int intervalMs(int idleMs, int workingMs) const { return m_workingMode ? workingMs : idleMs; }
    void markDetected(int64_t nowMs) { m_lastDetectionTime = nowMs; }
    int64_t lastDetectionTime() const { return m_lastDetectionTime; }

    // ===== 调度: 低频(空闲)/高频(有人洗车)模式切换 =====
    bool workingMode() const { return m_workingMode; }
    void enterWorking();
    void enterIdle();

    // ===== 业务判定: 升降频 / 人只报一次 / 异常变化才报。状态都在区域内部。 =====
    void evaluate(const std::vector<DetectionResult::Item>& items, int64_t ts,
                  int exitWorkingEmptyTimes, std::vector<DetectionAlarm>& alarms);

    // ===== 查询 =====
    const std::string& id() const { return m_binding.regionId; }
    std::string modelIds() const;   // 本轮参与检测的模型 id(逗号拼接, 落库用)

private:
    struct ModelBinding {
        washcar::cfg::AiModelConfig cfg;
        std::map<int, DetectionResult::Type> classMap;   // class_id -> 业务类型
        std::shared_ptr<washcar::ai::IAiModel> model;    // 与其他区域共享的同一个实例
    };

    const std::vector<ModelBinding>* activeModels() const;

    washcar::cfg::AiRegionBinding m_binding;
    std::vector<ModelBinding> m_idleModels;      // 低频模式参与的模型(车、人)
    std::vector<ModelBinding> m_workingModels;   // 高频模式参与的模型(人、车顶杂物、车门、车窗、车牌)

    // 调度状态
    bool m_workingMode = false;
    int64_t m_lastDetectionTime = 0;

    // 业务判定状态
    int m_consecutiveEmpty = 0;
    bool m_personReported = false;
    std::map<DetectionResult::Type, bool> m_abnormalState;
};
