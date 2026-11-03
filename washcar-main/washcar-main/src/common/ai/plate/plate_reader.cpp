#include "common/ai/plate/plate_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/ai/ai_model.h"
#include "common/log/log_manager.h"
#include "rknn_api.h"

namespace washcar {
namespace plate {

// 官方 Model Zoo 的 plate_code 顺序: 31 省份 + 10 数字 + 24 字母(不含 I/O) + I + O + '-'
// 末位 '-' 兼作 CTC 空白(类号 = 字符数 - 1)
// 注意: 省份是 UTF-8 多字节, 必须按字符建表, 不能按字节切
const std::vector<std::string>& PlateReader::charset() {
    static const std::vector<std::string> table = {
        "京", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
        "苏", "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
        "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁",
        "新",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "A", "B", "C", "D", "E", "F", "G", "H", "J", "K",
        "L", "M", "N", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z", "I", "O", "-"
    };
    return table;
}

PlateReader::~PlateReader() {
    unload();
}

bool PlateReader::load(const std::string& modelPath) {
    unload();
    if (!ai::loadRknnContext(modelPath, m_ctx)) {
        LOG_ERROR("[Plate] reader model load failed: " + modelPath);
        return false;
    }
    // 输出张量 [1, 类别数, 时间步], 类别数必须与字符表一致
    if (m_ctx->io_num.n_output < 1) {
        LOG_ERROR("[Plate] reader model has no output: " + modelPath);
        unload();
        return false;
    }
    if (m_ctx->output_attrs[0].n_dims < 3) {
        LOG_ERROR("[Plate] reader output rank too low: " + modelPath +
                  ", n_dims=" + std::to_string(m_ctx->output_attrs[0].n_dims));
        unload();
        return false;
    }
    // 输入尺寸必须跟调用方给的缓冲一致:read() 收的是固定 94x24x3 的缓冲,
    // 模型要是别的尺寸,rknn_inputs_set 会按模型尺寸去读这块缓冲,越界。
    if (m_ctx->model_width != kPlateWidth || m_ctx->model_height != kPlateHeight ||
        m_ctx->model_channel != 3) {
        LOG_ERROR("[Plate] reader input size mismatch: " + modelPath +
                  ", got " + std::to_string(m_ctx->model_width) + "x" +
                  std::to_string(m_ctx->model_height) + "x" +
                  std::to_string(m_ctx->model_channel) +
                  ", want 94x24x3");
        unload();
        return false;
    }
    m_numClasses = m_ctx->output_attrs[0].dims[1];
    m_timeSteps  = m_ctx->output_attrs[0].dims[2];
    const int expected = (int)charset().size();
    if (m_numClasses != expected) {
        LOG_ERROR("[Plate] reader output classes=" + std::to_string(m_numClasses) +
                  " != charset size " + std::to_string(expected));
        unload();
        return false;
    }
    LOG_INFO("[Plate] reader loaded: " + modelPath + ", classes=" + std::to_string(m_numClasses) +
             ", timeSteps=" + std::to_string(m_timeSteps));
    return true;
}

void PlateReader::unload() {
    ai::releaseRknnContext(m_ctx);
    m_timeSteps = 0;
    m_numClasses = 0;
}

bool PlateReader::read(const uint8_t* rgb24x94, std::string& plate, float& confidence) {
    plate.clear();
    confidence = 0.0f;
    if (!m_ctx || !rgb24x94 || m_timeSteps <= 0 || m_numClasses <= 0) return false;

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = (uint32_t)(kPlateWidth * kPlateHeight * 3);
    input.buf = const_cast<uint8_t*>(rgb24x94);
    if (rknn_inputs_set(m_ctx->rknn_ctx, 1, &input) < 0) return false;
    if (rknn_run(m_ctx->rknn_ctx, nullptr) < 0) return false;

    rknn_output output;
    memset(&output, 0, sizeof(output));
    output.want_float = 1;
    if (rknn_outputs_get(m_ctx->rknn_ctx, 1, &output, nullptr) < 0) return false;
    const float* buf = (const float*)output.buf;

    // 内存布局 [类别][时间步]; 逐时间步取最大概率类别
    const int blank = m_numClasses - 1;
    const auto& table = charset();
    int prev = -1;
    double confSum = 0.0;
    int confCount = 0;
    for (int t = 0; t < m_timeSteps; ++t) {
        int best = 0;
        float bestVal = -1e30f;
        for (int c = 0; c < m_numClasses; ++c) {
            float v = buf[(size_t)c * m_timeSteps + t];
            if (v > bestVal) { bestVal = v; best = c; }
        }
        if (best == blank) { prev = -1; continue; }
        if (best == prev) continue;      // CTC 连续重复只留一个
        prev = best;
        plate += table[best];
        // 模型输出是 logit(实测可小于 0), 过 sigmoid 才是概率
        confSum += 1.0 / (1.0 + std::exp(-(double)bestVal));
        confCount++;
    }
    rknn_outputs_release(m_ctx->rknn_ctx, 1, &output);

    if (confCount > 0) confidence = (float)(confSum / confCount);
    // 首位不是省份字说明首字没读准, 置信度打折(现场 1080p 下首位省份字最不稳)
    bool provinceOk = false;
    if (!plate.empty()) {
        for (size_t i = 0; i <= 30 && i < table.size(); ++i) {
            if (plate[0] == table[i][0]) { provinceOk = true; break; }
        }
    }
    if (!provinceOk) confidence *= 0.5f;
    return true;
}

}  // namespace plate
}  // namespace washcar
