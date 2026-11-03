#include "common/ai/image_ops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace washcar {
namespace image {

namespace {

inline uint8_t clamp8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

// 双线性取一个 RGB 像素; 越界时钳到边缘
inline void sampleRgb(const uint8_t* src, int sw, int sh, float x, float y, uint8_t* out) {
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > sw - 1.0f) x = (float)(sw - 1);
    if (y > sh - 1.0f) y = (float)(sh - 1);
    int x0 = (int)x, y0 = (int)y;
    int x1 = std::min(x0 + 1, sw - 1);
    int y1 = std::min(y0 + 1, sh - 1);
    float wx = x - x0, wy = y - y0;
    for (int c = 0; c < 3; ++c) {
        float p00 = src[(y0 * sw + x0) * 3 + c];
        float p01 = src[(y0 * sw + x1) * 3 + c];
        float p10 = src[(y1 * sw + x0) * 3 + c];
        float p11 = src[(y1 * sw + x1) * 3 + c];
        float top = p00 + (p01 - p00) * wx;
        float bot = p10 + (p11 - p10) * wx;
        out[c] = clamp8(top + (bot - top) * wy);
    }
}

// 解 8x8 线性方程组(高斯消元带部分主元); 解不出来返回 false
bool solve8(double a[8][9], double out[8]) {
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 8; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        }
        if (std::fabs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) {
            for (int c = col; c < 9; ++c) std::swap(a[col][c], a[pivot][c]);
        }
        for (int r = col + 1; r < 8; ++r) {
            double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (int c = col; c < 9; ++c) a[r][c] -= f * a[col][c];
        }
    }
    for (int r = 7; r >= 0; --r) {
        double sum = a[r][8];
        for (int c = r + 1; c < 8; ++c) sum -= a[r][c] * out[c];
        out[r] = sum / a[r][r];
    }
    return true;
}

}  // namespace

void resizeRgb(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    const float xRatio = (float)srcW / (float)dstW;
    const float yRatio = (float)srcH / (float)dstH;
    for (int y = 0; y < dstH; ++y) {
        float sy = (y + 0.5f) * yRatio - 0.5f;
        if (sy < 0.0f) sy = 0.0f;
        if (sy > srcH - 1.0f) sy = (float)(srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            float sx = (x + 0.5f) * xRatio - 0.5f;
            sampleRgb(src, srcW, srcH, sx, sy, dst + (y * dstW + x) * 3);
        }
    }
}

void toGray(const uint8_t* rgb, int w, int h, uint8_t* gray) {
    if (!rgb || !gray || w <= 0 || h <= 0) return;
    for (int i = 0; i < w * h; ++i) {
        float v = 0.299f * rgb[i * 3] + 0.587f * rgb[i * 3 + 1] + 0.114f * rgb[i * 3 + 2];
        gray[i] = clamp8(v);
    }
}

void clahe(const uint8_t* gray, int w, int h, uint8_t* out, double clipLimit, int gridSize) {
    if (!gray || !out || w <= 0 || h <= 0) return;
    int gx = std::max(1, gridSize);
    int gy = std::max(1, gridSize);
    if (gx > w) gx = w;
    if (gy > h) gy = h;

    const int blockW = (w + gx - 1) / gx;
    const int blockH = (h + gy - 1) / gy;

    // 每块直方图 -> 限幅重分配 -> 累积分布成 LUT
    std::vector<std::array<int, 256>> hist((size_t)gx * gy);
    for (auto& e : hist) e.fill(0);
    for (int by = 0; by < gy; ++by) {
        int y0 = by * blockH, y1 = std::min(y0 + blockH, h);
        for (int bx = 0; bx < gx; ++bx) {
            int x0 = bx * blockW, x1 = std::min(x0 + blockW, w);
            auto& e = hist[(size_t)by * gx + bx];
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row = gray + (size_t)y * w;
                for (int x = x0; x < x1; ++x) e[row[x]]++;
            }
        }
    }

    const int clipCount = std::max(1, (int)(clipLimit * (double)blockW * (double)blockH / 256.0));
    std::vector<std::array<uint8_t, 256>> luts(hist.size());
    for (size_t i = 0; i < hist.size(); ++i) {
        auto& e = hist[i];
        int excess = 0;
        for (int b = 0; b < 256; ++b) {
            if (e[b] > clipCount) {
                excess += e[b] - clipCount;
                e[b] = clipCount;
            }
        }
        int share = excess / 256;
        int rest = excess - share * 256;
        for (int b = 0; b < 256; ++b) e[b] += share;
        e[0] += rest;   // 余数补给最低灰度级

        int acc = 0;
        int total = blockW * blockH;
        for (int b = 0; b < 256; ++b) {
            acc += e[b];
            luts[i][b] = (uint8_t)std::min(255, (int)((double)acc * 255.0 / (double)total));
        }
    }

    // 双线性插值相邻 4 块的 LUT
    for (int y = 0; y < h; ++y) {
        float fy = ((float)y + 0.5f) / (float)blockH - 0.5f;
        int by0 = (int)std::floor(fy);
        float wy = fy - by0;
        int by1 = by0 + 1;
        if (by0 < 0) { by0 = 0; wy = 0.0f; }
        if (by1 < 0) by1 = 0;
        if (by1 >= gy) by1 = gy - 1;
        if (by0 >= gy) by0 = gy - 1;

        for (int x = 0; x < w; ++x) {
            float fx = ((float)x + 0.5f) / (float)blockW - 0.5f;
            int bx0 = (int)std::floor(fx);
            float wx = fx - bx0;
            int bx1 = bx0 + 1;
            if (bx0 < 0) { bx0 = 0; wx = 0.0f; }
            if (bx1 < 0) bx1 = 0;
            if (bx1 >= gx) bx1 = gx - 1;
            if (bx0 >= gx) bx0 = gx - 1;

            const auto& l00 = luts[(size_t)by0 * gx + bx0];
            const auto& l01 = luts[(size_t)by0 * gx + bx1];
            const auto& l10 = luts[(size_t)by1 * gx + bx0];
            const auto& l11 = luts[(size_t)by1 * gx + bx1];

            int v = gray[(size_t)y * w + x];
            float t00 = l00[v], t01 = l01[v], t10 = l10[v], t11 = l11[v];
            float top = t00 + (t01 - t00) * wx;
            float bot = t10 + (t11 - t10) * wx;
            out[(size_t)y * w + x] = clamp8(top + (bot - top) * wy);
        }
    }
}

void warpPerspectiveRgb(const uint8_t* src, int srcW, int srcH,
                        const float quad[4][2], uint8_t* dst, int dstW, int dstH) {
    if (!src || !dst || !quad || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    // 单应矩阵: 源四点 -> 目标矩形
    const float dstQuad[4][2] = {
        {0.0f, 0.0f},
        {(float)(dstW - 1), 0.0f},
        {(float)(dstW - 1), (float)(dstH - 1)},
        {0.0f, (float)(dstH - 1)},
    };
    double m[8][9] = {{0}};
    for (int i = 0; i < 4; ++i) {
        double x = quad[i][0], y = quad[i][1];
        double u = dstQuad[i][0], v = dstQuad[i][1];
        double* r0 = m[i * 2];
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[6] = -u * x; r0[7] = -u * y; r0[8] = u;
        double* r1 = m[i * 2 + 1];
        r1[3] = x; r1[4] = y; r1[5] = 1; r1[6] = -v * x; r1[7] = -v * y; r1[8] = v;
    }
    double h[8] = {0};
    if (!solve8(m, h)) return;   // 退化(共线)时不做变换

    // 3x3 求逆, 做反向映射
    double a = h[0], b = h[1], c = h[2];
    double d = h[3], e = h[4], f = h[5];
    double g = h[6], i = h[7], j = 1.0;
    double det = a * (e * j - f * i) - b * (d * j - f * g) + c * (d * i - e * g);
    if (std::fabs(det) < 1e-12) return;
    double inv[9];
    inv[0] = (e * j - f * i) / det;
    inv[1] = (c * i - b * j) / det;
    inv[2] = (b * f - c * e) / det;
    inv[3] = (f * g - d * j) / det;
    inv[4] = (a * j - c * g) / det;
    inv[5] = (c * d - a * f) / det;
    inv[6] = (d * i - e * g) / det;
    inv[7] = (b * g - a * i) / det;
    inv[8] = (a * e - b * d) / det;

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            double px = x, py = y;
            double denom = inv[6] * px + inv[7] * py + inv[8];
            if (std::fabs(denom) < 1e-12) {
                std::memset(dst + (y * dstW + x) * 3, 0, 3);
                continue;
            }
            double sx = (inv[0] * px + inv[1] * py + inv[2]) / denom;
            double sy = (inv[3] * px + inv[4] * py + inv[5]) / denom;
            sampleRgb(src, srcW, srcH, (float)sx, (float)sy, dst + (y * dstW + x) * 3);
        }
    }
}

}  // namespace image
}  // namespace washcar
