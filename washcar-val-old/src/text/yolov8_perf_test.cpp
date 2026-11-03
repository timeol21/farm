// 模型检测效率测试: 真实 yolov8.rknn 对测试图连续推理 N 次,
// 输出首次/平均耗时, 把首帧检测框(类别名+置信度标签)画到图上存 jpg.
// 用法: yolov8_perf_test [model] [img] [out]
// 模型/示例图随项目放在 lib/rknn/models; 运行时不传参则按候选路径自动定位
// (项目相对路径优先, 兼容盒子 build/src 运行目录, 盒子固定测试资源兜底);
// 产物默认写到运行目录下(和日志一致), 可用 argv[3] 覆盖.
#include "common/ai/yolov8/yolov8.h"
#include "font8x8_basic.h"
#include "image_utils.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// COCO 80 类名(与 yolov8 输出 cls_id 对应)
const char* kCocoNames[80] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
    "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator",
    "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"};

void drawBox(uint8_t* rgb, int w, int h, const image_rect_t& b, int thickness = 3) {
    auto put = [&](int x, int y) {
        if (x < 0) x = 0;
        if (x >= w) x = w - 1;
        if (y < 0) y = 0;
        if (y >= h) y = h - 1;
        uint8_t* p = rgb + static_cast<size_t>(y) * w * 3 + static_cast<size_t>(x) * 3;
        p[0] = 255; p[1] = 0; p[2] = 0;   // 红色框
    };
    for (int t = 0; t < thickness; ++t) {
        for (int x = b.left - t; x <= b.right + t; ++x) {
            put(x, b.top - t);
            put(x, b.bottom + t);
        }
        for (int y = b.top - t; y <= b.bottom + t; ++y) {
            put(b.left - t, y);
            put(b.right + t, y);
        }
    }
}

// 8x8 位图字体放大 scale 倍绘制, 黑底白字, 超出画布的部分裁掉
void drawLabel(uint8_t* rgb, int w, int h, int x, int y, const char* text, int scale = 2) {
    const int cw = 8 * scale, ch = 8 * scale;
    for (int i = 0; text[i]; ++i) {
        unsigned char ch8 = static_cast<unsigned char>(text[i]);
        if (ch8 >= 128) ch8 = '?';
        for (int r = 0; r < 8; ++r) {
            unsigned char row = font8x8_basic[ch8][r];
            for (int c = 0; c < 8; ++c) {
                bool on = (row >> c) & 1;   // 表内 bit0 = 最左列(LSB-left)
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        int px = x + i * cw + c * scale + dx;
                        int py = y + r * scale + dy;
                        if (px < 0 || px >= w || py < 0 || py >= h) continue;
                        uint8_t* p = rgb + static_cast<size_t>(py) * w * 3 + static_cast<size_t>(px) * 3;
                        if (on) {
                            p[0] = 255; p[1] = 255; p[2] = 255;   // 白字
                        } else {
                            p[0] = 0; p[1] = 0; p[2] = 0;         // 黑底
                        }
                    }
                }
            }
        }
    }
}

std::string locate(const std::vector<std::string>& candidates) {
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string modelPath = argc > 1
        ? argv[1]
        : locate({
              "lib/rknn/models/yolov8.rknn",               // 随项目走(cwd=项目根/源树)
              "../../../../src/lib/rknn/models/yolov8.rknn",  // 盒子 build/src 运行目录上溯
              "/home/ztl/models/yolov8.rknn",              // 盒子固定测试资源兜底
          });
    const std::string imgPath = argc > 2
        ? argv[2]
        : locate({
              "lib/rknn/models/bus.jpg",
              "../../../../src/lib/rknn/models/bus.jpg",
              "/home/ztl/models/bus.jpg",
          });
    const char* outPath = argc > 3 ? argv[3] : "bus_detected.jpg";   // 运行目录下(和日志一致)

    if (modelPath.empty() || imgPath.empty()) {
        std::printf("model/img not found (model='%s' img='%s'), pass argv[1]/argv[2]\n",
                    modelPath.c_str(), imgPath.c_str());
        return 1;
    }
    std::printf("model=%s\nimg=%s\nout=%s\n", modelPath.c_str(), imgPath.c_str(), outPath);

    rknn_app_context_t ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    if (init_yolov8_model(modelPath.c_str(), &ctx) != 0) {
        std::printf("init_yolov8_model fail: %s\n", modelPath.c_str());
        return 1;
    }

    image_buffer_t img;
    std::memset(&img, 0, sizeof(img));
    if (read_image(imgPath.c_str(), &img) != 0 || !img.virt_addr) {
        std::printf("read_image fail: %s\n", imgPath.c_str());
        return 1;
    }
    std::printf("image %dx%d size=%d\n", img.width, img.height, img.size);

    // 连续推理 N 次: 首次含 NPU 预热, 分别报首次/平均
    const int N = 10;
    double total = 0.0, first = 0.0;
    for (int i = 0; i < N; ++i) {
        object_detect_result_list od;
        std::memset(&od, 0, sizeof(od));
        auto t0 = std::chrono::steady_clock::now();
        int ret = inference_yolov8_model(&ctx, &img, &od);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (i == 0) {
            first = ms;
        } else {
            total += ms;
        }
        std::printf("infer #%d ret=%d  %.1f ms  objects=%d\n", i, ret, ms, od.count);
        if (i == 0) {
            for (int j = 0; j < od.count; ++j) {
                const object_detect_result& r = od.results[j];
                const char* name = (r.cls_id >= 0 && r.cls_id < 80) ? kCocoNames[r.cls_id] : "?";
                std::printf("  obj[%d] cls=%d(%s) conf=%.3f box=(%d,%d)-(%d,%d)\n", j, r.cls_id,
                            name, r.prop, r.box.left, r.box.top, r.box.right, r.box.bottom);
                drawBox(img.virt_addr, img.width, img.height, r.box);
                // 标签画在框上方, 顶到画布上边缘则移到框内
                char label[64];
                std::snprintf(label, sizeof(label), "%s %.2f", name, r.prop);
                int labelY = r.box.top - 8 * 2 - 2;
                if (labelY < 0) labelY = r.box.top + 4;
                drawLabel(img.virt_addr, img.width, img.height, r.box.left, labelY, label);
            }
            write_image(outPath, &img);
            std::printf("saved: %s\n", outPath);
        }
    }
    std::printf("first %.1f ms, avg(2..%d) %.1f ms\n", first, N, total / (N - 1));

    std::free(img.virt_addr);
    release_yolov8_model(&ctx);
    return 0;
}
