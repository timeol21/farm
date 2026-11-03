#pragma once

#include <cstdint>

namespace washcar {
namespace image {

// 图像原语: 项目不依赖 opencv, 缩放/提亮/透视变换自己实现
// 全部走 CPU —— RGA 在 1920x1080 非等比缩放时会触发内核崩溃(见 CMakeLists DISABLE_RGA)

// 双线性缩放 RGB888 -> RGB888
void resizeRgb(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH);

// RGB888 -> 单通道灰度
void toGray(const uint8_t* rgb, int w, int h, uint8_t* gray);

// 对比度受限自适应直方图均衡(提亮暗部, 夜间阴影里的车牌靠它才看得清)
void clahe(const uint8_t* gray, int w, int h, uint8_t* out, double clipLimit, int gridSize);

// 四点透视变换: 把源图里的 quad 四边形拉正成 dstW x dstH 的 RGB888
// quad 顺序: 左上, 右上, 右下, 左下
void warpPerspectiveRgb(const uint8_t* src, int srcW, int srcH,
                        const float quad[4][2], uint8_t* dst, int dstW, int dstH);

}  // namespace image
}  // namespace washcar
