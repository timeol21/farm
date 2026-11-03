#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/config/config_object.h"
#include "yolov8.h"   // rknn_app_context_t: 通用 rknn 上下文容器

namespace washcar {
namespace ai {

// 模型看到的图像: 外部帧的 RGB888 视图, 不拷贝数据
struct AiImage {
    const uint8_t* rgb   = nullptr;
    int            width = 0;
    int            height = 0;
};

// 模型命中项: 与业务枚举解耦, 检测服务按 classMap 映射成业务项
struct AiModelHit {
    int         classId    = -1;    // 模型输出的类别 id
    bool        detected  = false;
    std::string value;              // 文本型结果(车牌号等), 纯检测模型留空
    float       confidence = 0.0f;
    std::string imagePath;          // 留证图片路径(模型自己存的, 如车牌裁剪图)
    float       box[4] = {0, 0, 0, 0};   // 命中位置(原图坐标 x1,y1,x2,y2)
};

// AI 模型统一入口: 加载/卸载/推理
// 模型种类不同(目标检测/车牌两级串联)只是内部实现差异, 由子类表达
class IAiModel {
public:
    virtual ~IAiModel() = default;

    virtual bool load(const cfg::AiModelConfig& cfg) = 0;
    virtual void unload() = 0;
    virtual std::vector<AiModelHit> run(const AiImage& image) = 0;
};

// 按配置里的 modelKind 造对应模型; 未知 kind 返回 nullptr
std::unique_ptr<IAiModel> createAiModel(const std::string& modelKind);

// 载入 rknn 模型: 读文件 + rknn_init + 查输入输出属性 + 记输入尺寸与量化方式
// 成功时 ctx 由调用方持有, 用 releaseRknnContext 释放
bool loadRknnContext(const std::string& path, rknn_app_context_t*& ctx);
void releaseRknnContext(rknn_app_context_t*& ctx);

}  // namespace ai
}  // namespace washcar
