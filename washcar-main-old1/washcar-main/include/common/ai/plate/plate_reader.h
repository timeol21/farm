#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "yolov8.h"   // rknn_app_context_t

namespace washcar {
namespace plate {

// 车牌字符识别: 吃一张 24x94 的车牌图, 吐车牌号
// 模型输出是 CTC 序列(每步 68 类), 解码后去重去空白
class PlateReader {
public:
    ~PlateReader();

    bool load(const std::string& modelPath);
    void unload();

    // rgb24x94: 24 行 94 列的 RGB888 车牌图; plate 出识别串, confidence 出置信度
    bool read(const uint8_t* rgb24x94, std::string& plate, float& confidence);

    // 字符表(68 个), 索引即模型输出类别; 最后一个 '-' 兼作 CTC 空白
    static const std::vector<std::string>& charset();

private:
    rknn_app_context_t* m_ctx = nullptr;
    int m_timeSteps = 0;    // CTC 时间步
    int m_numClasses = 0;   // 字符类别数
};

}  // namespace plate
}  // namespace washcar
