// =====================================================================
// 原生 x86_64 构建用的 turbojpeg 桩
//
// 为什么需要:仓库里的 libturbojpeg.a 只有 aarch64 版。
// turbojpeg 是纯软件 JPEG 库,x86 上本来有系统包,但为了"零安装依赖、
// 只验逻辑"的原生构建不引入系统包,这里用同签名空实现顶掉。
//
// 影响范围:turbojpeg 只被 lib/rknn/utils/image_utils.c 的 AI 图像预处理
// 和存图路径用到,不在业务逻辑路径上。桩全部返回失败,任何误用都会立刻
// 暴露成"AI 图像处理失败",不会静默产出错数据。
// =====================================================================
#include "turbojpeg.h"

#include <stdio.h>

static int g_warned = 0;

static int stub_fail(void) {
    if (!g_warned) {
        g_warned = 1;
        fprintf(stderr, "[native-stub] libturbojpeg 是桩实现,AI 图像 JPEG 编解码不可用\n");
    }
    return -1;
}

tjhandle tjInitCompress(void) {
    stub_fail();
    return NULL;
}

tjhandle tjInitDecompress(void) {
    stub_fail();
    return NULL;
}

int tjCompress2(tjhandle handle, const unsigned char* srcBuf, int width, int pitch,
                int height, int pixelFormat, unsigned char** jpegBuf,
                unsigned long* jpegSize, int jpegSubsamp, int jpegQual, int flags) {
    (void)handle;
    (void)srcBuf;
    (void)width;
    (void)pitch;
    (void)height;
    (void)pixelFormat;
    (void)jpegBuf;
    (void)jpegSize;
    (void)jpegSubsamp;
    (void)jpegQual;
    (void)flags;
    return stub_fail();
}

int tjDecompressHeader3(tjhandle handle, const unsigned char* jpegBuf, unsigned long jpegSize,
                        int* width, int* height, int* jpegSubsamp, int* jpegColorspace) {
    (void)handle;
    (void)jpegBuf;
    (void)jpegSize;
    (void)width;
    (void)height;
    (void)jpegSubsamp;
    (void)jpegColorspace;
    return stub_fail();
}

int tjDecompress2(tjhandle handle, const unsigned char* jpegBuf, unsigned long jpegSize,
                   unsigned char* dstBuf, int width, int pitch, int height, int pixelFormat,
                   int flags) {
    (void)handle;
    (void)jpegBuf;
    (void)jpegSize;
    (void)dstBuf;
    (void)width;
    (void)pitch;
    (void)height;
    (void)pixelFormat;
    (void)flags;
    return stub_fail();
}

int tjDestroy(tjhandle handle) {
    (void)handle;
    return 0;
}

void tjFree(unsigned char* buffer) {
    (void)buffer;
}

int tjGetErrorCode(tjhandle handle) {
    (void)handle;
    return stub_fail();
}

char* tjGetErrorStr(void) {
    stub_fail();
    return "native-stub: turbojpeg 不可用";
}
