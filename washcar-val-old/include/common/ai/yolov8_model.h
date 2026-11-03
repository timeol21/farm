#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/ai/ai_model.h"
#include "yolov8.h"

namespace washcar {
namespace ai {

// 通用目标检测模型(yolov8 系列): 一次推理给出多个类别的框
class Yolov8Model : public IAiModel {
public:
    Yolov8Model() = default;
    ~Yolov8Model() override;

    bool load(const cfg::AiModelConfig& cfg) override;
    void unload() override;
    std::vector<AiModelHit> run(const AiImage& image) override;

private:
    rknn_app_context_t* m_ctx = nullptr;
};

}  // namespace ai
}  // namespace washcar
