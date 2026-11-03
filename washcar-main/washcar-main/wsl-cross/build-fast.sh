#!/usr/bin/env bash
# =============================================================================
# WSL 交叉编译 washcar (RK3568 / aarch64) —— 盒子产物的构建入口
#
# 为什么源码要搬到 WSL 原生盘:
#   项目源码在 /mnt/c(Windows 盘), WSL 访问它走 9p 协议, 读头文件极慢。
#   做法: 源码 rsync 到 WSL 原生盘, 在那边编译, 产物也留在原生盘。
#   实测(2026-10-23): 冷构建 97s → 65s(快 33%); 增量两者持平(2-4s)。
#   同步后校验源码指纹 —— 保证"编的就是当前这份", 不会在 Windows IDE 改完忘同步。
#
# 为什么只剩这一个构建脚本:
#   原来的 build-wsl.sh(源码留 /mnt/c)已被本脚本全面取代。而且两者共用同一个
#   构建目录 ~/washcar-build —— 交替用会互相把对方的 CMake 缓存和 ccache 作废
#   (源码路径不同 → 重新 configure + 全量重编 75-97s)。实测 2026-10-23。
#   build-wsl.sh 独有的 configure / rebuild / clean 子命令已并入本脚本。
#
# 用法(WSL 中,项目根目录下执行):
#   bash wsl-cross/build-fast.sh              # configure(如需要) + build
#   bash wsl-cross/build-fast.sh configure    # 强制重新 configure
#   bash wsl-cross/build-fast.sh rebuild      # clean + configure + build
#   bash wsl-cross/build-fast.sh clean        # 删除构建目录
#   bash wsl-cross/build-fast.sh --sync-only  # 只同步源码不编译
#
# 环境变量:
#   JOBS=4              并行编译数(默认 4)
#   BUILD_TYPE=Release  CMake 构建类型(默认 Release)
#   BUILD_TARGETS=main  只编指定 target(默认 main)
#   BUILD_DIR=<path>    构建目录(默认 $HOME/washcar-build)
#
# 产物: $HOME/washcar-build/<target>  (deploy-wsl.sh 默认就从这里取)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 和 skill 共用同一份 WSL 源码副本和构建目录, 避免两套源码互相打架。
WORK="${WSL_FAST_DIR:-${HOME}/washcar-wsl}"
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
JOBS="${JOBS:-4}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_TARGETS="${BUILD_TARGETS:-main}"
ACTION="${1:-build}"

case "$ACTION" in
    build|configure|rebuild|clean|--sync-only) ;;
    *) echo "用法: $0 {build|configure|rebuild|clean|--sync-only}" >&2; exit 2 ;;
esac

# clean 不需要同步源码, 也不需要 ccache
if [ "$ACTION" = "clean" ]; then
    rm -rf "${BUILD_DIR}"
    echo ">>> Removed ${BUILD_DIR}"
    exit 0
fi

# 源码指纹: 只算参与编译的源文件
# 用 -print0 / xargs -0: 文件名带空格(项目里有 "memory allocation.h")时不会算错
fingerprint() {
    ( cd "$1" && find src include lib/rknn/utils -type f \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' \) -print0 \
        | sort -z | xargs -0 md5sum | md5sum | cut -d' ' -f1 )
}

echo ">>> [1/4] 同步源码到原生盘: ${WORK}"
mkdir -p "${WORK}"
# 排除历史构建产物和缓存: 它们跟编译无关, 每次白搬一遍很慢
rsync -a --delete \
    --exclude 'build-wsl' --exclude 'build-wsl-fast' --exclude '.git' \
    --exclude '.ccache-wsl' --exclude 'node_modules' --exclude '.vs' \
    --exclude '/out/' --exclude '/build/' --exclude '/build-aarch64/' \
    --exclude '/tools/' \
    "${PROJECT_ROOT}/" "${WORK}/"

if [ "$(fingerprint "${PROJECT_ROOT}")" != "$(fingerprint "${WORK}")" ]; then
    echo "ERROR: 同步后源码指纹不一致, 拒绝编译(避免编了旧代码)" >&2
    exit 1
fi
echo "    源码指纹一致, 可以编"

if [ "$ACTION" = "--sync-only" ]; then
    echo ">>> 只同步完成"
    exit 0
fi

export CCACHE_DIR="${CCACHE_DIR:-${HOME}/.ccache-wsl}"
export CCACHE_MAXSIZE=10G
export CCACHE_SLOPPINESS=time_macros,file_macro,include_file_mtime,include_file_ctime,system_headers
mkdir -p "${CCACHE_DIR}" "${BUILD_DIR}"

TOOLCHAIN="${WORK}/wsl-cross/toolchain-aarch64-wsl.cmake"

do_configure() {
    echo ">>> configure (BUILD_TYPE=${BUILD_TYPE}, BUILD_DIR=${BUILD_DIR})"
    # 只清 cache, 不 rm 整个 build 目录: 保留测试产物和 ccache 命中的对象。
    rm -f "${BUILD_DIR}/CMakeCache.txt"
    cmake -S "${WORK}" -B "${BUILD_DIR}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" >/dev/null
}

echo ">>> [2/4] configure"
if [ "$ACTION" = "rebuild" ]; then
    rm -rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
    do_configure
elif [ "$ACTION" = "configure" ]; then
    do_configure
elif [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] \
    || [ "${TOOLCHAIN}" -nt "${BUILD_DIR}/CMakeCache.txt" ] \
    || ! grep -qF "CMAKE_HOME_DIRECTORY:INTERNAL=${WORK}" "${BUILD_DIR}/CMakeCache.txt" \
    || ! grep -qF "CMAKE_BUILD_TYPE:STRING=${BUILD_TYPE}" "${BUILD_DIR}/CMakeCache.txt"; then
    do_configure
else
    echo "    缓存有效, 跳过 configure"
fi

if [ "$ACTION" = "configure" ]; then
    echo ">>> configure 完成, 没编译"
    exit 0
fi

echo ">>> [3/4] build: ${BUILD_TARGETS} (JOBS=${JOBS})"
LOG="${BUILD_DIR}/build-fast.log"
if ! cmake --build "${BUILD_DIR}" -j "${JOBS}" --target ${BUILD_TARGETS} >"${LOG}" 2>&1; then
    echo ">>> BUILD FAILED. 最后 120 行:" >&2
    tail -n 120 "${LOG}" >&2
    echo ">>> 完整日志: ${LOG}" >&2
    exit 1
fi
tail -n 30 "${LOG}"

echo ">>> [4/4] 产物:"
for t in ${BUILD_TARGETS}; do
    SRC="${BUILD_DIR}/src/${t}"
    DST="${BUILD_DIR}/${t}"
    if [ -f "${SRC}" ]; then
        aarch64-linux-gnu-strip --strip-debug -o "${DST}" "${SRC}"
    fi
    if [ ! -f "${DST}" ]; then
        echo "ERROR: target ${t} 没有产物 (${SRC} / ${DST})" >&2
        exit 4
    fi
    file "${DST}" | grep -q "ARM aarch64" || {
        echo "ERROR: ${DST} 不是 ARM aarch64" >&2
        file "${DST}" >&2
        exit 5
    }
    echo "    ARTIFACT=${DST} ($(du -h "${DST}" | cut -f1))"
done
echo ">>> 部署: bash wsl-cross/deploy-wsl.sh"
