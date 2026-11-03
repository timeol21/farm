// =====================================================================
// 原生 x86_64 构建用的 RKNN 桩
//
// 为什么需要:仓库里的 librknnrt.so 只有 aarch64 版(瑞芯微 NPU 运行时),
// x86_64 上没法链接。这里用同签名的空实现顶掉,让"只验逻辑"的原生构建能编过。
//
// 行为约定:全部返回 RKNN_ERR_FAIL,调用方(ai_model.cpp / yolov8.cc)会走
// "模型加载失败"分支并打日志 —— 这是期望行为,原生构建本来就没有 NPU。
// 第一次调用时打一行 stderr 提示,避免"桩悄悄生效"导致误判。
// =====================================================================
#include "rknn_api.h"

#include <stdio.h>

static int g_warned = 0;

static int stub_fail(void) {
    if (!g_warned) {
        g_warned = 1;
        fprintf(stderr, "[native-stub] librknnrt 是桩实现(x86_64 无 NPU),AI 推理路径不可用\n");
    }
    return RKNN_ERR_FAIL;
}

int rknn_init(rknn_context* context, void* model, uint32_t size, uint32_t flag,
               rknn_init_extend* extend) {
    (void)model;
    (void)size;
    (void)flag;
    (void)extend;
    if (context != NULL) {
        *context = 0;
    }
    return stub_fail();
}

int rknn_destroy(rknn_context context) {
    (void)context;
    return RKNN_SUCC;
}

int rknn_query(rknn_context context, rknn_query_cmd cmd, void* info, uint32_t size) {
    (void)context;
    (void)cmd;
    (void)info;
    (void)size;
    return stub_fail();
}

int rknn_inputs_set(rknn_context context, uint32_t n_inputs, rknn_input inputs[]) {
    (void)context;
    (void)n_inputs;
    (void)inputs;
    return stub_fail();
}

int rknn_run(rknn_context context, rknn_run_extend* extend) {
    (void)context;
    (void)extend;
    return stub_fail();
}

int rknn_outputs_get(rknn_context context, uint32_t n_outputs, rknn_output outputs[],
                      rknn_output_extend* extend) {
    (void)context;
    (void)n_outputs;
    (void)outputs;
    (void)extend;
    return stub_fail();
}

int rknn_outputs_release(rknn_context context, uint32_t n_outputs, rknn_output outputs[]) {
    (void)context;
    (void)n_outputs;
    (void)outputs;
    return RKNN_SUCC;
}
