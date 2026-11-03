#include "common/ai/plate/plate_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/ai/image_ops.h"
#include "common/log/log_manager.h"
#include "image_utils.h"

namespace washcar {
namespace plate {

namespace {

// 洗车机出口侧摄像头固定, 车停位固定 -> 车牌落在画面里一个可配置的范围内
// 整幅 1920x1080 直接送检测会漏掉车牌(车牌只占千分之几), 必须先裁 ROI
constexpr float kMinAspect = 1.8f;      // 车牌宽高比下限(含外扩)
constexpr float kMaxAspect = 4.5f;      // 上限
constexpr float kMinScore  = 0.5f;      // 框分数门槛
constexpr int   kWarpW = 188;           // 透视矫正目标宽(与 94x24 同比例, 留 CLAHE 空间)
constexpr int   kWarpH = 86;

}  // namespace

PlateModel::~PlateModel() {
    unload();
}

bool PlateModel::load(const cfg::AiModelConfig& cfg) {
    unload();
    // modelPaths 顺序即 [检测模型, 识别模型]
    if (cfg.modelPaths.size() < 2) {
        LOG_ERROR("[Plate] modelPaths 需要两个文件(检测+识别), modelId=" + cfg.modelId);
        return false;
    }
    if (!m_detector.load(cfg.modelPaths[0])) return false;
    if (!m_reader.load(cfg.modelPaths[1])) {
        m_detector.unload();
        return false;
    }
    m_roiX = cfg.roi.x;
    m_roiY = cfg.roi.y;
    m_roiW = cfg.roi.w;
    m_roiH = cfg.roi.h;
    m_warp.resize((size_t)kWarpW * kWarpH * 3);
    m_gray.resize((size_t)kWarpW * kWarpH);
    m_enh.resize((size_t)kWarpW * kWarpH);
    m_small.resize(94 * 24 * 3);
    LOG_INFO("[Plate] model loaded: " + cfg.modelId + ", roi=" + std::to_string(m_roiX) + "," +
             std::to_string(m_roiY) + "," + std::to_string(m_roiW) + "," + std::to_string(m_roiH));
    return true;
}

void PlateModel::unload() {
    m_detector.unload();
    m_reader.unload();
    m_roiX = m_roiY = m_roiW = m_roiH = 0;
}

std::vector<ai::AiModelHit> PlateModel::run(const ai::AiImage& image) {
    std::vector<ai::AiModelHit> hits;
    if (!image.rgb || image.width <= 0 || image.height <= 0) return hits;

    // 1. 裁 ROI: 整幅太宽, 车牌在里面太小检测不到
    int rx = 0, ry = 0, rw = image.width, rh = image.height;
    if (m_roiW > 0 && m_roiH > 0) {
        rx = std::max(0, std::min(m_roiX, image.width - 1));
        ry = std::max(0, std::min(m_roiY, image.height - 1));
        rw = std::min(m_roiW, image.width - rx);
        rh = std::min(m_roiH, image.height - ry);
    }
    if (rw <= 8 || rh <= 8) return hits;

    // ROI 的 RGB 是原图的一块, 单独拷一份喂检测(检测输出的是 ROI 坐标)
    std::vector<uint8_t> roi((size_t)rw * rh * 3);
    for (int y = 0; y < rh; ++y) {
        memcpy(roi.data() + (size_t)y * rw * 3,
               image.rgb + ((size_t)(ry + y) * image.width + rx) * 3,
               (size_t)rw * 3);
    }

    // 2. 文本行检测
    std::vector<PlateBox> boxes = m_detector.detect(roi.data(), rw, rh);
    for (const auto& b : boxes) {
        LOG_INFO("[Plate] box x[" + std::to_string((int)b.left) + "," + std::to_string((int)b.right) +
                 "] y[" + std::to_string((int)b.top) + "," + std::to_string((int)b.bottom) +
                 "] score=" + std::to_string(b.score));
    }
    const bool debug = getenv("PLATE_DEBUG") != nullptr;
    if (debug) {
        image_buffer_t dbg = {};
        dbg.width = rw; dbg.height = rh; dbg.format = IMAGE_FORMAT_RGB888;
        dbg.size = (int)roi.size(); dbg.virt_addr = roi.data(); dbg.fd = 0;
        write_image("/tmp/plate_roi.jpg", &dbg);
    }

    // 3. 按宽高比筛车牌, 取分数最高的
    const PlateBox* best = nullptr;
    for (const auto& b : boxes) {
        float bw = b.right - b.left;
        float bh = b.bottom - b.top;
        if (bw <= 4.0f || bh <= 4.0f) continue;
        float aspect = bw / bh;
        if (aspect < kMinAspect || aspect > kMaxAspect) continue;
        if (b.score < kMinScore) continue;
        if (!best || b.score > best->score) best = &b;
    }

    ai::AiModelHit hit;
    hit.classId = 0;
    hit.detected = false;
    hits.push_back(hit);
    if (!best) return hits;

    hit.detected = true;
    hit.confidence = best->score;
    // 车牌框(原图坐标, ROI 局部 + ROI 偏移 = 整幅坐标)
    hit.box[0] = best->left + rx;
    hit.box[1] = best->top + ry;
    hit.box[2] = best->right + rx;
    hit.box[3] = best->bottom + ry;

    // 4. 透视矫正 + 提亮: 车尾是斜的, 拉正后车牌字才认得出
    // 目标尺寸用框的自然边长(不放大): 放大再缩回会多一道插值模糊
    float quad[4][2];
    for (int i = 0; i < 4; ++i) {
        quad[i][0] = best->quad[i][0] + rx;   // ROI 坐标 -> 整幅坐标
        quad[i][1] = best->quad[i][1] + ry;
    }
    int warpW = (int)best->quadW;
    int warpH = (int)best->quadH;
    if (warpW < 32) warpW = 32;
    if (warpH < 12) warpH = 12;
    if ((size_t)warpW * warpH * 3 > m_warp.size()) {
        m_warp.resize((size_t)warpW * warpH * 3);
        m_gray.resize((size_t)warpW * warpH);
        m_enh.resize((size_t)warpH ? (size_t)warpW * warpH : 0);
    }
    image::warpPerspectiveRgb(image.rgb, image.width, image.height, quad, m_warp.data(), warpW, warpH);
    image::toGray(m_warp.data(), warpW, warpH, m_gray.data());
    // 车常停在阴影里, CLAHE 是让暗部车牌看得清的关键一步
    image::clahe(m_gray.data(), warpW, warpH, m_enh.data(), 3.0, 8);

    // 5. 拉到模型输入尺寸
    std::vector<uint8_t> rgb24((size_t)warpW * warpH * 3);
    for (int i = 0; i < warpW * warpH; ++i) {
        rgb24[i * 3] = m_enh[i];
        rgb24[i * 3 + 1] = m_enh[i];
        rgb24[i * 3 + 2] = m_enh[i];
    }
    image::resizeRgb(rgb24.data(), warpW, warpH, m_small.data(), 94, 24);

    // 6. 读号
    if (debug) {
        image_buffer_t dbg = {};
        dbg.width = 94; dbg.height = 24; dbg.format = IMAGE_FORMAT_RGB888;
        dbg.size = 94 * 24 * 3; dbg.virt_addr = m_small.data(); dbg.fd = 0;
        write_image("/tmp/plate_94x24.jpg", &dbg);
    }
    std::string plate;
    float conf = 0.0f;
    if (m_reader.read(m_small.data(), plate, conf)) {
        hit.value = plate;
        hit.confidence = std::min(hit.confidence, conf);
    }
    hits[0] = hit;
    return hits;
}

}  // namespace plate
}  // namespace washcar
