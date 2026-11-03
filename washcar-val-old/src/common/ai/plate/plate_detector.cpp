#include "common/ai/plate/plate_detector.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "common/ai/ai_model.h"
#include "common/ai/image_ops.h"
#include "common/log/log_manager.h"
#include "rknn_api.h"

namespace washcar {
namespace plate {

namespace {

constexpr float kProbThreshold = 0.3f;    // DB 像素概率阈值
constexpr float kUnclipRatio   = 1.5f;    // DB 外扩比例(与官方 example 一致)
constexpr int   kMinBoxPixels  = 40;      // 太小的连通域丢掉(噪点)

struct Component {
    std::vector<std::pair<int, int>> pixels;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
};

// 主方向拟合出的旋转框(模型空间)
struct RotRect {
    float cx = 0, cy = 0;      // 中心
    float w = 0, h = 0;        // 长边, 短边
    float angle = 0;           // 长边与 x 轴夹角(弧度)
};

// 用像素集合的主方向(PCA)拟合旋转框
RotRect fitRotRect(const std::vector<std::pair<int, int>>& px) {
    RotRect r;
    if (px.size() < 2) return r;
    double mx = 0, my = 0;
    for (const auto& p : px) { mx += p.first; my += p.second; }
    mx /= px.size(); my /= px.size();

    double sxx = 0, syy = 0, sxy = 0;
    for (const auto& p : px) {
        double dx = p.first - mx, dy = p.second - my;
        sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
    }
    double theta = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    double ux = std::cos(theta), uy = std::sin(theta);

    double minU = 1e18, maxU = -1e18, minV = 1e18, maxV = -1e18;
    for (const auto& p : px) {
        double dx = p.first - mx, dy = p.second - my;
        double u = dx * ux + dy * uy;
        double v = -dx * uy + dy * ux;
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
    }
    r.cx = (float)mx; r.cy = (float)my;
    r.w = (float)(maxU - minU) + 1.0f;
    r.h = (float)(maxV - minV) + 1.0f;
    r.angle = (float)theta;
    if (r.h > r.w) {   // 保证长边是 w
        std::swap(r.w, r.h);
        r.angle += (float)(M_PI / 2.0);
    }
    return r;
}

void rotRectToQuad(const RotRect& r, float quad[4][2]) {
    double ux = std::cos(r.angle), uy = std::sin(r.angle);
    double vx = -uy, vy = ux;
    double hw = r.w / 2.0, hh = r.h / 2.0;
    double cx = r.cx, cy = r.cy;
    quad[0][0] = (float)(cx - hw * ux - hh * vx);   // 左上
    quad[0][1] = (float)(cy - hw * uy - hh * vy);
    quad[1][0] = (float)(cx + hw * ux - hh * vx);   // 右上
    quad[1][1] = (float)(cy + hw * uy - hh * vy);
    quad[2][0] = (float)(cx + hw * ux + hh * vx);   // 右下
    quad[2][1] = (float)(cy + hw * uy + hh * vy);
    quad[3][0] = (float)(cx - hw * ux + hh * vx);   // 左下
    quad[3][1] = (float)(cy - hw * uy + hh * vy);
}

// DB 的 unclip: 按面积/周长折算外扩距离, 矩形上等价于四周各扩 d
float unclipDistance(float w, float h, float ratio) {
    float perimeter = 2.0f * (w + h);
    if (perimeter <= 0.0f) return 0.0f;
    return w * h * ratio / perimeter;
}

}  // namespace

PlateDetector::~PlateDetector() {
    unload();
}

bool PlateDetector::load(const std::string& modelPath) {
    unload();
    if (!ai::loadRknnContext(modelPath, m_ctx)) {
        LOG_ERROR("[Plate] detector model load failed: " + modelPath);
        return false;
    }
    m_probW = m_ctx->model_width;
    m_probH = m_ctx->model_height;
    m_scaled.resize((size_t)m_probW * m_probH * 3);
    m_probF.resize((size_t)m_probW * m_probH);
    LOG_INFO("[Plate] detector loaded: " + modelPath + ", input=" + std::to_string(m_probW) + "x" + std::to_string(m_probH));
    return true;
}

void PlateDetector::unload() {
    ai::releaseRknnContext(m_ctx);
    m_probW = m_probH = 0;
}

std::vector<PlateBox> PlateDetector::detect(const uint8_t* rgb, int width, int height) {
    std::vector<PlateBox> out;
    if (!m_ctx || !rgb || width <= 0 || height <= 0) return out;

    // 1. 整幅缩放到模型输入(非等比, 与官方 example 一致)
    image::resizeRgb(rgb, width, height, m_scaled.data(), m_probW, m_probH);

    // 2. 推理
    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = (uint32_t)(m_probW * m_probH * m_ctx->model_channel);
    input.buf = m_scaled.data();
    if (rknn_inputs_set(m_ctx->rknn_ctx, 1, &input) < 0) return out;
    if (rknn_run(m_ctx->rknn_ctx, nullptr) < 0) return out;

    rknn_output output;
    memset(&output, 0, sizeof(output));
    output.want_float = 1;
    if (rknn_outputs_get(m_ctx->rknn_ctx, 1, &output, nullptr) < 0) return out;
    const float* prob = (const float*)output.buf;
    m_probF.assign(prob, prob + (size_t)m_probW * m_probH);
    rknn_outputs_release(m_ctx->rknn_ctx, 1, &output);

    // 3. 二值化 + 连通域
    const int pw = m_probW, ph = m_probH;
    std::vector<uint8_t> bin((size_t)pw * ph, 0);
    for (int i = 0; i < pw * ph; ++i) {
        bin[i] = m_probF[i] > kProbThreshold ? 1 : 0;
    }

    std::vector<Component> comps;
    std::vector<int> label((size_t)pw * ph, -1);
    std::vector<int> stack;
    const int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int start = 0; start < pw * ph; ++start) {
        if (!bin[start] || label[start] >= 0) continue;
        int id = (int)comps.size();
        comps.emplace_back();
        Component& c = comps[id];
        c.minX = c.maxX = start % pw;
        c.minY = c.maxY = start / pw;
        stack.clear();
        stack.push_back(start);
        label[start] = id;
        while (!stack.empty()) {
            int p = stack.back();
            stack.pop_back();
            int y = p / pw, x = p % pw;
            c.pixels.emplace_back(x, y);
            c.minX = std::min(c.minX, x); c.maxX = std::max(c.maxX, x);
            c.minY = std::min(c.minY, y); c.maxY = std::max(c.maxY, y);
            for (int k = 0; k < 8; ++k) {
                int nx = x + dx8[k], ny = y + dy8[k];
                if (nx < 0 || ny < 0 || nx >= pw || ny >= ph) continue;
                int np = ny * pw + nx;
                if (!bin[np] || label[np] >= 0) continue;
                label[np] = id;
                stack.push_back(np);
            }
        }
        if ((int)c.pixels.size() < kMinBoxPixels) {
            c.pixels.clear();
            label[start] = -2;   // 丢弃
        }
    }

    // 4. 每域拟合旋转框, 算 unclip 后的轴对齐范围
    struct Cand {
        std::vector<const Component*> comps;
        float minX, minY, maxX, maxY;
        float score;
    };
    std::vector<Cand> cands;
    for (const auto& c : comps) {
        if (c.pixels.empty()) continue;
        RotRect r = fitRotRect(c.pixels);
        float d = unclipDistance(r.w, r.h, kUnclipRatio);
        float halfW = r.w / 2.0f + d, halfH = r.h / 2.0f + d;
        double ux = std::cos(r.angle), uy = std::sin(r.angle);
        double vx = -uy, vy = ux;
        float ex[4][2];
        for (int i = 0; i < 4; ++i) {
            double sx = (i == 0 || i == 3) ? -1.0 : 1.0;
            double sy = (i < 2) ? -1.0 : 1.0;
            ex[i][0] = (float)(r.cx + sx * halfW * ux + sy * halfH * vx);
            ex[i][1] = (float)(r.cy + sx * halfW * uy + sy * halfH * vy);
        }
        float minX = ex[0][0], maxX = ex[0][0], minY = ex[0][1], maxY = ex[0][1];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, ex[i][0]); maxX = std::max(maxX, ex[i][0]);
            minY = std::min(minY, ex[i][1]); maxY = std::max(maxY, ex[i][1]);
        }
        double sum = 0;
        for (const auto& p : c.pixels) sum += m_probF[(size_t)p.second * pw + p.first];
        cands.push_back({{&c}, minX, minY, maxX, maxY, (float)(sum / c.pixels.size())});
    }

    // 5. 合并相邻框(同一行文字): 垂直重叠够 + 水平间隙小
    std::vector<int> groupOf(cands.size(), -1);
    std::vector<std::vector<int>> groups;
    for (size_t i = 0; i < cands.size(); ++i) {
        for (size_t g = 0; g < groups.size(); ++g) {
            float gminX = cands[groups[g][0]].minX, gmaxX = cands[groups[g][0]].maxX;
            float gminY = cands[groups[g][0]].minY, gmaxY = cands[groups[g][0]].maxY;
            float cminX = cands[i].minX, cmaxX = cands[i].maxX;
            float cminY = cands[i].minY, cmaxY = cands[i].maxY;
            for (const auto& idx : groups[g]) {
                gminX = std::min(gminX, cands[idx].minX); gmaxX = std::max(gmaxX, cands[idx].maxX);
                gminY = std::min(gminY, cands[idx].minY); gmaxY = std::max(gmaxY, cands[idx].maxY);
            }
            float vOverlap = std::min(gmaxY, cmaxY) - std::max(gminY, cminY);
            float minH = std::min(gmaxY - gminY, cmaxY - cminY);
            if (minH <= 0.0f) continue;
            if (vOverlap / minH < 0.4f) continue;
            float gap = std::max(0.0f, std::max(gminX, cminX) - std::min(gmaxX, cmaxX));
            if (gap > minH * 0.9f) continue;
            groups[g].push_back((int)i);
            groupOf[i] = (int)g;
            break;
        }
        if (groupOf[i] < 0) {
            groups.push_back({(int)i});
            groupOf[i] = (int)groups.size() - 1;
        }
    }

    // 6. 每组用全部像素重拟合旋转框, 映射回原图坐标
    const float scaleX = (float)width / (float)pw;
    const float scaleY = (float)height / (float)ph;
    for (const auto& g : groups) {
        std::vector<std::pair<int, int>> all;
        double sum = 0;
        size_t n = 0;
        for (int idx : g) {
            all.insert(all.end(), cands[idx].comps[0]->pixels.begin(), cands[idx].comps[0]->pixels.end());
            sum += cands[idx].score;
            n += cands[idx].comps[0]->pixels.size();
        }
        if (all.size() < kMinBoxPixels) continue;
        RotRect r = fitRotRect(all);
        if (r.w < 8.0f || r.h < 4.0f) continue;
        // DB 的最终框是 unclip 外扩过的: 文本框是紧贴字符的, 不外扩会切掉首尾字符
        // (实测外扩后横向约 1.4 倍, 正好把省份字完整包进来)
        float d = unclipDistance(r.w, r.h, kUnclipRatio);
        r.w += d * 2.0f;
        r.h += d * 2.0f;

        PlateBox box;
        rotRectToQuad(r, box.quad);
        float minX = box.quad[0][0], maxX = box.quad[0][0];
        float minY = box.quad[0][1], maxY = box.quad[0][1];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, box.quad[i][0]); maxX = std::max(maxX, box.quad[i][0]);
            minY = std::min(minY, box.quad[i][1]); maxY = std::max(maxY, box.quad[i][1]);
        }
        box.left = minX * scaleX;
        box.top = minY * scaleY;
        box.right = maxX * scaleX;
        box.bottom = maxY * scaleY;
        for (int i = 0; i < 4; ++i) {
            box.quad[i][0] *= scaleX;
            box.quad[i][1] *= scaleY;
        }
        box.score = (float)(sum / g.size());
        box.quadW = r.w;
        box.quadH = r.h;
        out.push_back(box);
    }
    return out;
}

}  // namespace plate
}  // namespace washcar
