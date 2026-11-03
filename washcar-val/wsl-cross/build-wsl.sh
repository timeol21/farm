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
#   BUILD_DIR=<path>    构建目录（默认 本项目/build-wsl，见下）
#   DEPLOY_DIR=<path>   可运行文件目录（默认 本项目/deployed）
#   VERBOSE=1           打印完整编译输出
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 构建目录固定在**本项目内**的 build-wsl/，不与其他项目共用。
#
# 背景: washcar-main(正式版) 和 washcar-val(测试版) 是两套独立代码,
# 若构建目录都落在 $HOME/washcar-build, 后编的那套会覆盖前者, 产物张冠李戴。
# 因此这里用基于 PROJECT_ROOT 的绝对路径, 两套代码各编各的, 互不干扰。
# 项目 .gitignore 里已排除 /build-wsl/, 不会污染仓库。
#
# 代价: 源码和构建目录都在 /mnt/e (9p 协议) 上, 比放 WSL 原生盘慢一些。
# 实测(JOBS=4, 68 个编译单元, ccache 已热):
#   无改动空跑 0.54s (原生盘 0.19s) / 改单个 .cpp 12.3s (原生盘 3.7s) / 全量 72.5s (原生盘 64s)
# 绝对时间可以接受。若某次临时需要更快: BUILD_DIR=$HOME/tmp-build bash wsl-cross/build-wsl.sh
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build-wsl}"

# 可运行文件（main + libmosquitto.so.1 + config.json）统一落在项目根的 deployed/。
# 规则：所有可运行产物只准出现在项目目录内，不往 $HOME / /tmp 写任何东西。
#   build-on-box.sh 用的是同一个目录（RUN_DIR 默认也是 $SRC/deployed），
#   所以两条编译路线的产物位置一致，切换时不用改部署脚本。
DEPLOY_DIR="${DEPLOY_DIR:-${PROJECT_ROOT}/deployed}"

TOOLCHAIN="${SCRIPT_DIR}/toolchain-aarch64-wsl.cmake"
JOBS="${JOBS:-4}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
ACTION="${1:-build}"

# ccache
# 缓存放项目内 .ccache/，不写 $HOME —— 整个项目自包含，删目录就干净。
# 代价: 在 /mnt/e (9p) 上比 WSL 原生盘慢，但省掉"缓存散落在用户目录、换机器就没了"的麻烦。
export CCACHE_DIR="${CCACHE_DIR:-${PROJECT_ROOT}/.ccache}"
export CCACHE_MAXSIZE=10G
export CCACHE_SLOPPINESS=time_macros,file_macro,include_file_mtime,include_file_ctime,system_headers
mkdir -p "$CCACHE_DIR" "$BUILD_DIR" "$DEPLOY_DIR"

# 找仓库内置的 libmosquitto 实体文件。
# 不能用固定文件名——Windows 上传/zip 解压后 third_party 里的 .so / .so.1 软链会丢，
# 只剩 libmosquitto.so.1.6.9。所以按目录 glob，短名字优先。
find_vendor_mosq() {
    local d f
    for d in "$PROJECT_ROOT/third_party/mosquitto/lib/aarch64" "$PROJECT_ROOT/third_party/mosquitto/lib"; do
        [ -d "$d" ] || continue
        for f in "$d/libmosquitto.so" "$d/libmosquitto.so.1" "$d"/libmosquitto.so.*; do
            [ -s "$f" ] && { echo "$f"; return 0; }   # -s: 跳过 0 字节空壳
        done
    done
    return 1
}

# 把运行所需的文件补齐到 deployed/。
# 只有三样东西：main（上一步已 strip 过来）+ libmosquitto.so.1 + config.json。
# 程序按 /proc/self/exe 定位自己的目录，所以 config.json 必须和 main 同级。
assemble_deploy() {
    mkdir -p "$DEPLOY_DIR"

    # NEEDED 里写的是 libmosquitto.so.1，目标文件名必须叫这个（不能是 .so.1.6.9）
    local so
    so="$(find_vendor_mosq || true)"
    if [ -n "$so" ]; then
        cp -L "$so" "$DEPLOY_DIR/libmosquitto.so.1"
        echo "  [deploy] libmosquitto.so.1 <- ${so#"$PROJECT_ROOT"/}"
    else
        echo "  [deploy] 警告: 没找到内置 libmosquitto，盒子上需系统里有这个库" >&2
    fi

    # 配置文件：项目根有就用项目的，没有则首次运行自动生成模板
    if [ -f "$PROJECT_ROOT/config.json" ]; then
        cp "$PROJECT_ROOT/config.json" "$DEPLOY_DIR/config.json"
        echo "  [deploy] config.json"
    else
        echo "  [deploy] 项目根没有 config.json，首次运行会用内置默认值生成模板"
    fi
}

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

    # strip 后直接落到 deployed/（可运行目录），build-wsl 里只留带符号的 .o
    for t in $targets; do
        local src="$BUILD_DIR/src/$t"
        local dst="$DEPLOY_DIR/$t"
        if [ -f "$src" ]; then
            aarch64-linux-gnu-strip --strip-debug -o "$dst" "$src"
            echo "  [strip] $t -> deployed/$t: $(du -h "$dst" | cut -f1) (unstripped: $(du -h "$src" | cut -f1))"
        fi
    done

    # 组装运行目录：main 已就位，再补 libmosquitto.so.1 和 config.json
    assemble_deploy

    # 验证架构（每个 target）
    for t in $targets; do
        DST="$DEPLOY_DIR/$t"
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
    echo ">>> 可运行目录: ${DEPLOY_DIR}"
    ls -lh "$DEPLOY_DIR" 2>/dev/null | sed 's/^/    /'
    echo ">>> 部署到盒子: bash wsl-cross/deploy-wsl.sh"
elif [ "$ACTION" = "configure" ]; then
    echo ""
    echo ">>> Configure only done."
fi
