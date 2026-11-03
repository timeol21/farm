#!/usr/bin/env bash
# =============================================================================
# WSL 交叉编译 washcar (RK3568 / aarch64)
#
# 用法（在 WSL 中，项目根目录下执行）：
#   bash wsl-cross/build-wsl.sh              # configure(如需要) + build
#   bash wsl-cross/build-wsl.sh configure    # 强制重新 configure
#   bash wsl-cross/build-wsl.sh rebuild      # clean + configure + build
#   bash wsl-cross/build-wsl.sh clean        # 删除 build-wsl
#
# 环境变量：
#   JOBS=4              并行编译数（默认 4）
#   BUILD_TYPE=Debug    CMake 构建类型（默认 Debug）
#   BUILD_TARGETS=main  只编指定 target（默认 main）
#   BUILD_DIR=<path>    构建目录（默认 $HOME/washcar-build，见下）
#   VERBOSE=1           打印完整编译输出
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 构建目录放 WSL 原生盘, 不要放 /mnt/c:
# 源码在 Windows 盘时读头文件已经慢一个数量级(实测单文件语法检查 3.2s vs 0.37s),
# .o 和最终二进制再写回 /mnt/c 就更慢。构建目录放原生盘能省掉最大的一块 IO。
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
TOOLCHAIN="${SCRIPT_DIR}/toolchain-aarch64-wsl.cmake"
JOBS="${JOBS:-4}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
ACTION="${1:-build}"

# ccache
# 缓存在 WSL 原生盘: 放 /mnt/c 的话缓存读写也走 9p 协议, 白费缓存的意义
export CCACHE_DIR="${CCACHE_DIR:-${HOME}/.ccache-wsl}"
export CCACHE_MAXSIZE=10G
export CCACHE_SLOPPINESS=time_macros,file_macro,include_file_mtime,include_file_ctime,system_headers
mkdir -p "$CCACHE_DIR" "$BUILD_DIR"

do_configure() {
    echo ">>> Configuring (BUILD_TYPE=${BUILD_TYPE})..."
    rm -rf "$BUILD_DIR"
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    echo ">>> Configure done."
}

do_build() {
    local targets="${BUILD_TARGETS:-main}"
    echo ">>> Building: ${targets} (JOBS=${JOBS})"

    if [ "${VERBOSE:-0}" = "1" ]; then
        cmake --build "$BUILD_DIR" -j "$JOBS" ${targets:+--target} $targets
    else
        cmake --build "$BUILD_DIR" -j "$JOBS" ${targets:+--target} $targets 2>&1 | tail -50
    fi

    # strip 每个 target
    for t in $targets; do
        SRC="$BUILD_DIR/src/$t"
        DST="$BUILD_DIR/$t"
        if [ -f "$SRC" ]; then
            aarch64-linux-gnu-strip --strip-debug -o "$DST" "$SRC"
            echo "  [strip] $t: $(du -h "$DST" | cut -f1) (unstripped: $(du -h "$SRC" | cut -f1))"
        fi
    done

    # 验证架构（每个 target）
    for t in $targets; do
        DST="$BUILD_DIR/$t"
        if [ -f "$DST" ]; then
            echo ""
            echo ">>> 产物验证 ($t):"
            file "$DST" | head -1
            aarch64-linux-gnu-readelf -d "$DST" 2>/dev/null | grep NEEDED | head -5
            echo "  ..."
        fi
    done
}

case "$ACTION" in
    configure)
        do_configure
        ;;
    rebuild)
        do_configure
        do_build
        ;;
    build)
        # toolchain 改了必须重新 configure:CMAKE_EXE_LINKER_FLAGS 是 CACHE 变量,
        # 缓存不刷新的话新加的链接参数(如 -Wl,-rpath,$ORIGIN)不会生效 —— 编译过、产物却不对。
        if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || [ "$TOOLCHAIN" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
            echo ">>> No CMakeCache or toolchain changed, configuring first..."
            do_configure
        fi
        do_build
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        echo ">>> Removed $BUILD_DIR"
        ;;
    *)
        echo "Usage: $0 {configure|build|rebuild|clean}" >&2
        exit 2
        ;;
esac

if [ "$ACTION" = "build" ] || [ "$ACTION" = "rebuild" ]; then
    echo ""
    for t in ${BUILD_TARGETS:-main}; do
        echo ">>> Done. Stripped binary: ${BUILD_DIR}/$t"
    done
    echo ">>> 部署到盒子: bash wsl-cross/deploy-wsl.sh"
elif [ "$ACTION" = "configure" ]; then
    echo ""
    echo ">>> Configure only done."
fi
