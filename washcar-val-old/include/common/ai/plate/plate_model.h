#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/ai/ai_model.h"
#include "common/ai/plate/plate_detector.h"
#include "common/ai/plate/plate_reader.h"

namespace washcar {
namespace plate {

// 车牌模型(两级串联): ROI -> 文本行检测 -> 按宽高比筛出车牌 -> 透视矫正提亮 -> 字符识别
// 对外仍是一个模型: 一次 run 给出车牌有无 + 车牌号
class PlateModel : public ai::IAiModel {
public:
    PlateModel() = default;
    ~PlateModel() override;

    bool load(const cfg::AiModelConfig& cfg) override;
    void unload() override;
    std::vector<ai::AiModelHit> run(const ai::AiImage& image) override;

private:
    PlateDetector m_detector;
    PlateReader   m_reader;

    int m_roiX = 0, m_roiY = 0, m_roiW = 0, m_roiH = 0;   // 原图坐标的 ROI; roiW<=0 = 整幅

    std::vector<uint8_t> m_warp;    // 透视矫正后的车牌图
    std::vector<uint8_t> m_gray;    // 灰度
    std::vector<uint8_t> m_enh;     // CLAHE 提亮后
    std::vector<uint8_t> m_small;   // 24x94 模型输入
};

}  // namespace plate
}  // namespace washcar
