#include "common/ai/yolov8_model.h"

#include <cstring>

#include "common/log/log_manager.h"

namespace washcar {
namespace ai {

Yolov8Model::~Yolov8Model() {
    unload();
}

bool Yolov8Model::load(const cfg::AiModelConfig& cfg) {
    unload();
    if (cfg.modelPaths.empty()) {
        LOG_ERROR("[Ai] yolov8 modelPaths empty, modelId=" + cfg.modelId);
        return false;
    }
    if (!loadRknnContext(cfg.modelPaths[0], m_ctx)) {
        LOG_ERROR("[Ai] yolov8 model load failed: " + cfg.modelPaths[0]);
        return false;
    }
    LOG_INFO("[Ai] yolov8 model loaded: " + cfg.modelId + ", path=" + cfg.modelPaths[0] +
             ", input=" + std::to_string(m_ctx->model_width) + "x" + std::to_string(m_ctx->model_height));
    return true;
}

void Yolov8Model::unload() {
    releaseRknnContext(m_ctx);
}

std::vector<AiModelHit> Yolov8Model::run(const AiImage& image) {
    std::vector<AiModelHit> hits;
    if (!m_ctx || !image.rgb || image.width <= 0 || image.height <= 0) return hits;

    image_buffer_t img = {};
    img.width = image.width;
    img.height = image.height;
    img.width_stride = image.width;
    img.height_stride = image.height;
    img.format = IMAGE_FORMAT_RGB888;
    img.size = image.width * image.height * 3;
    img.virt_addr = const_cast<uint8_t*>(image.rgb);
    img.fd = 0;

    object_detect_result_list odResults = {};
    int ret = inference_yolov8_model(m_ctx, &img, &odResults);
    if (ret != RKNN_SUCC) {
        LOG_WARNING("[Ai] inference_yolov8_model failed, ret=" + std::to_string(ret));
        return hits;
    }
    for (int i = 0; i < odResults.count; ++i) {
        const auto& det = odResults.results[i];
        AiModelHit hit;
        hit.classId = det.cls_id;
        hit.detected = true;
        hit.confidence = det.prop;
        // 框(left,top,right,bottom) 已经是 x1,y1,x2,y2, 原图坐标, 配原图才是完整证据
        hit.box[0] = det.box.left;
        hit.box[1] = det.box.top;
        hit.box[2] = det.box.right;
        hit.box[3] = det.box.bottom;
        hits.push_back(hit);
    }
    return hits;
}

}  // namespace ai
}  // namespace washcar
