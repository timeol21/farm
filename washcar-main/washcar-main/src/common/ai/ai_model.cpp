#include "common/ai/ai_model.h"

#include <cstring>
#include <vector>

#include "common/ai/plate/plate_model.h"
#include "common/ai/yolov8_model.h"
#include "common/log/log_manager.h"
#include "file_utils.h"
#include "rknn_api.h"

namespace washcar {
namespace ai {

bool loadRknnContext(const std::string& path, rknn_app_context_t*& ctx) {
    releaseRknnContext(ctx);

    char* modelData = nullptr;
    int modelLen = read_data_from_file(path.c_str(), &modelData);
    if (modelData == nullptr || modelLen <= 0) {
        if (modelData) free(modelData);
        LOG_ERROR("[Ai] read model file failed: " + path);
        return false;
    }

    rknn_app_context_t* c = new rknn_app_context_t;
    memset(c, 0, sizeof(*c));
    int ret = rknn_init(&c->rknn_ctx, modelData, modelLen, 0, nullptr);
    free(modelData);
    if (ret != RKNN_SUCC) {
        delete c;
        LOG_ERROR("[Ai] rknn_init failed: " + path + ", ret=" + std::to_string(ret));
        return false;
    }

    if (rknn_query(c->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &c->io_num, sizeof(c->io_num)) != RKNN_SUCC) {
        rknn_destroy(c->rknn_ctx);
        delete c;
        LOG_ERROR("[Ai] rknn_query io_num failed: " + path);
        return false;
    }

    // 输入输出张量数必须都 >= 1:下面直接取 inAttrs[0] / outAttrs[0],
    // 张量数为 0 时就是对空 vector 取下标(越界 UB),崩在没有上下文的地址上。
    if (c->io_num.n_input < 1 || c->io_num.n_output < 1) {
        rknn_destroy(c->rknn_ctx);
        delete c;
        LOG_ERROR("[Ai] model has no input/output tensor: " + path +
                  ", n_input=" + std::to_string(c->io_num.n_input) +
                  ", n_output=" + std::to_string(c->io_num.n_output));
        return false;
    }

    std::vector<rknn_tensor_attr> inAttrs(c->io_num.n_input);
    std::vector<rknn_tensor_attr> outAttrs(c->io_num.n_output);
    for (int i = 0; i < c->io_num.n_input; ++i) {
        inAttrs[i].index = i;
        if (rknn_query(c->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &inAttrs[i], sizeof(inAttrs[i])) != RKNN_SUCC) {
            rknn_destroy(c->rknn_ctx);
            delete c;
            LOG_ERROR("[Ai] rknn_query input attr failed: " + path);
            return false;
        }
    }
    for (int i = 0; i < c->io_num.n_output; ++i) {
        outAttrs[i].index = i;
        if (rknn_query(c->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &outAttrs[i], sizeof(outAttrs[i])) != RKNN_SUCC) {
            rknn_destroy(c->rknn_ctx);
            delete c;
            LOG_ERROR("[Ai] rknn_query output attr failed: " + path);
            return false;
        }
    }

    c->is_quant = (outAttrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC
                   && outAttrs[0].type == RKNN_TENSOR_INT8);
    if (inAttrs[0].fmt == RKNN_TENSOR_NCHW) {
        c->model_channel = inAttrs[0].dims[1];
        c->model_height  = inAttrs[0].dims[2];
        c->model_width   = inAttrs[0].dims[3];
    } else {
        c->model_height  = inAttrs[0].dims[1];
        c->model_width   = inAttrs[0].dims[2];
        c->model_channel = inAttrs[0].dims[3];
    }
    // 用 malloc/free 而不是 new[]:释放侧走 free(),两边必须配对。
    // new[] 配 free() 是未定义行为,现在多半不炸,但换分配器或换类型就变悬垂指针。
    c->input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * c->io_num.n_input);
    c->output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * c->io_num.n_output);
    if (c->input_attrs == nullptr || c->output_attrs == nullptr) {
        rknn_destroy(c->rknn_ctx);
        free(c->input_attrs);
        free(c->output_attrs);
        delete c;
        LOG_ERROR("[Ai] alloc tensor attrs failed: " + path);
        return false;
    }
    std::copy(inAttrs.begin(), inAttrs.end(), c->input_attrs);
    std::copy(outAttrs.begin(), outAttrs.end(), c->output_attrs);

    ctx = c;
    return true;
}

void releaseRknnContext(rknn_app_context_t*& ctx) {
    if (ctx == nullptr) return;
    if (ctx->input_attrs != nullptr) {
        free(ctx->input_attrs);
        ctx->input_attrs = nullptr;
    }
    if (ctx->output_attrs != nullptr) {
        free(ctx->output_attrs);
        ctx->output_attrs = nullptr;
    }
    if (ctx->rknn_ctx != 0) {
        rknn_destroy(ctx->rknn_ctx);
        ctx->rknn_ctx = 0;
    }
    delete ctx;
    ctx = nullptr;
}

std::unique_ptr<IAiModel> createAiModel(const std::string& modelKind) {
    if (modelKind == "yolov8") return std::make_unique<Yolov8Model>();
    if (modelKind == "plate")  return std::make_unique<plate::PlateModel>();
    LOG_ERROR("[Ai] unknown modelKind: " + modelKind);
    return nullptr;
}

}  // namespace ai
}  // namespace washcar
