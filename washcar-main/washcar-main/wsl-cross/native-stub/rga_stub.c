// =====================================================================
// 原生 x86_64 构建用的 RGA 桩
//
// 为什么需要:librga.so 是瑞芯微 RGA 硬件加速库,只有 aarch64 版。
// 项目已经全局 define(DISABLE_RGA)(RGA 在非等比缩放时会触发内核崩溃),
// 但 image_utils.c 里仍有少量 rga 符号被引用,原生链接需要这些符号存在。
//
// 行为约定:全部返回失败状态 / 空句柄。RGA 路径在原生构建里本来就不该走到。
// =====================================================================
#include "im2d.h"

#include <stdio.h>
#include <string.h>

static int g_warned = 0;

static IM_STATUS stub_status(void) {
    if (!g_warned) {
        g_warned = 1;
        fprintf(stderr, "[native-stub] librga 是桩实现(x86_64 无 RGA 硬件),图像硬件加速路径不可用\n");
    }
    return IM_STATUS_NOT_SUPPORTED;
}

IM_C_API const char* imStrError_t(IM_STATUS status) {
    (void)status;
    return "native-stub: RGA 硬件不可用";
}

IM_C_API IM_STATUS improcess(rga_buffer_t src, rga_buffer_t dst, rga_buffer_t pat,
                             im_rect srect, im_rect drect, im_rect prect, int usage) {
    (void)src;
    (void)dst;
    (void)pat;
    (void)srect;
    (void)drect;
    (void)prect;
    (void)usage;
    return stub_status();
}

IM_C_API IM_STATUS imfill_t(rga_buffer_t dst, im_rect rect, int color, int sync) {
    (void)dst;
    (void)rect;
    (void)color;
    (void)sync;
    return stub_status();
}

IM_EXPORT_API rga_buffer_handle_t importbuffer_fd(int fd, im_handle_param_t* param) {
    (void)fd;
    (void)param;
    return 0;
}

IM_EXPORT_API rga_buffer_handle_t importbuffer_virtualaddr(void* va, im_handle_param_t* param) {
    (void)va;
    (void)param;
    return 0;
}

IM_EXPORT_API rga_buffer_handle_t importbuffer_physicaladdr(uint64_t pa, im_handle_param_t* param) {
    (void)pa;
    (void)param;
    return 0;
}

IM_EXPORT_API IM_STATUS releasebuffer_handle(rga_buffer_handle_t handle) {
    (void)handle;
    return stub_status();
}

// wrapbuffer_*_t:把各种来源的 buffer 包成 rga_buffer_t。
// 原生构建没有 RGA 硬件,返回全零结构 —— 后续 improcess 会因参数非法失败。
IM_C_API rga_buffer_t wrapbuffer_handle_t(rga_buffer_handle_t handle, int width, int height,
                                          int wstride, int hstride, int format) {
    (void)handle;
    (void)width;
    (void)height;
    (void)wstride;
    (void)hstride;
    (void)format;
    rga_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    return buf;
}

IM_C_API rga_buffer_t wrapbuffer_virtualaddr_t(void* vir_addr, int width, int height,
                                               int wstride, int hstride, int format) {
    (void)vir_addr;
    (void)width;
    (void)height;
    (void)wstride;
    (void)hstride;
    (void)format;
    rga_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    return buf;
}

IM_C_API rga_buffer_t wrapbuffer_physicaladdr_t(void* phy_addr, int width, int height,
                                                int wstride, int hstride, int format) {
    (void)phy_addr;
    (void)width;
    (void)height;
    (void)wstride;
    (void)hstride;
    (void)format;
    rga_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    return buf;
}

IM_C_API rga_buffer_t wrapbuffer_fd_t(int fd, int width, int height,
                                      int wstride, int hstride, int format) {
    (void)fd;
    (void)width;
    (void)height;
    (void)wstride;
    (void)hstride;
    (void)format;
    rga_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    return buf;
}
