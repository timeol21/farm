#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "yolov8.h"   // rknn_app_context_t

namespace washcar {
namespace plate {

// 车牌候选框(原图坐标)
struct PlateBox {
    float       left = 0, top = 0, right = 0, bottom = 0;   // 轴对齐外接框
    float       quad[4][2] = {};                            // 旋转框四点: 左上,右上,右下,左下
    float       quadW = 0, quadH = 0;                       // 旋转框边长(透视矫正的目标尺寸)
    float       score = 0.0f;
};

// 文本行检测(PPOCR-Det / DB): 只认"哪里有文字", 不认是不是车牌
// 车牌要靠上层按宽高比 + 位置筛
class PlateDetector {
public:
    ~PlateDetector();

    bool load(const std::string& modelPath);
    void unload();

    // 在整幅 RGB888 图里找文本行, 返回原图坐标的候选框
    std::vector<PlateBox> detect(const uint8_t* rgb, int width, int height);

private:
    rknn_app_context_t* m_ctx = nullptr;
    int m_probW = 0, m_probH = 0;   // 模型输入尺寸(缩放目标)
    int m_outW = 0, m_outH = 0;     // 概率图尺寸(输出张量尺寸,与输入不一定相等)
    std::vector<uint8_t> m_scaled;   // 缩放后的 RGB888
    std::vector<uint8_t> m_prob;     // 概率图(量化输出转 float 后按 u8 存阈值化结果)
    std::vector<float>   m_probF;
};

}  // namespace plate
}  // namespace washcar
