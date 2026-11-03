#!/usr/bin/env bash
# =============================================================================
# WSL 原生盘快速编译
#
# 为什么需要这个脚本:
#   项目源码在 /mnt/c(Windows 盘), WSL 访问它走 9p 协议, 读头文件极慢。
#   实测同一个 .cpp 只做语法检查:
#     源码在 /mnt/c        -> 3.161s (其中 CPU 只占 0.5s, 其余全在等 IO)
#     源码在 WSL 原生盘   -> 0.373s          慢 8.5 倍
#   而每个 .cpp 都要 include 几十个头文件, 170 个编译单元 + 链接, 差距被放大到分钟级。
#
# 做法: 源码同步到 WSL 原生盘, 在那边编译, 产物留在原生盘。
#   同步后校验源码指纹 —— 保证"编的就是当前这份", 不会在 Windows IDE 改完忘同步。
#
# 用法:
#   bash wsl-cross/build-fast.sh                      # 编 main
#   BUILD_TARGETS="main detection_service_test" \
#   BUILD_TYPE=Release bash wsl-cross/build-fast.sh   # 编多个 target
#   bash wsl-cross/build-fast.sh --sync-only          # 只同步不编译
#
# 产物: $HOME/washcar-build/  (deploy-wsl.sh 默认就从这里取)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WORK="${WSL_FAST_DIR:-$HOME/washcar-src}"     # 原生盘上的源码副本
BUILD_DIR="${BUILD_DIR:-$HOME/washcar-build}"  # 原生盘上的构建目录
JOBS="${JOBS:-4}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_TARGETS="${BUILD_TARGETS:-main}"
ACTION="${1:-build}"

# 源码指纹: 只算参与编译的源文件
# 用 -print0 / xargs -0: 文件名带空格(项目里有 "memory allocation.h")时不会算错
fingerprint() {
    ( cd "$1" && find src include lib/rknn/utils -type f \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' \) -print0 \
        | sort -z | xargs -0 md5sum | md5sum | cut -d' ' -f1 )
}

echo ">>> [1/4] 同步源码到原生盘: $WORK"
mkdir -p "$WORK"
# 排除历史构建产物和缓存: 它们跟编译无关, 每次白搬一遍很慢
# (out/ 291MB、build/ 37MB、build-aarch64/ 17MB 都是旧构建残留)
rsync -a --delete \
    --exclude 'build-wsl' --exclude 'build-wsl-fast' --exclude '.git' \
    --exclude '.ccache-wsl' --exclude 'node_modules' --exclude '.vs' \
    --exclude '/out/' --exclude '/build/' --exclude '/build-aarch64/' \
    --exclude '/tools/' \
    "$PROJECT_ROOT/" "$WORK/"

if [ "$(fingerprint "$PROJECT_ROOT")" != "$(fingerprint "$WORK")" ]; then
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
mkdir -p "$CCACHE_DIR" "$BUILD_DIR"

echo ">>> [2/4] configure (BUILD_TYPE=${BUILD_TYPE}, BUILD_DIR=${BUILD_DIR})"
# 只在真需要时才重 configure。三种情况: 没有缓存 / toolchain 改过 / 源码目录换了。
# 以前是无条件 rm CMakeCache, 一次增量编译要多花 6 秒重新 configure
# (实测: 跳过 configure 后 10.2s -> 4.4s, 和走 /mnt/c 的 build-wsl.sh 持平)
TOOLCHAIN="$WORK/wsl-cross/toolchain-aarch64-wsl.cmake"
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] \
    || [ "$TOOLCHAIN" -nt "$BUILD_DIR/CMakeCache.txt" ] \
    || ! grep -qF "CMAKE_HOME_DIRECTORY:INTERNAL=$WORK" "$BUILD_DIR/CMakeCache.txt"; then
    rm -f "$BUILD_DIR/CMakeCache.txt"
    cmake -S "$WORK" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" >/dev/null
else
    echo "    缓存有效, 跳过 configure"
fi

echo ">>> [3/4] build: $BUILD_TARGETS (JOBS=$JOBS)"
# 不用 `| tail`: pipefail 已经保证编译失败会中止脚本, 这里直接出完整日志便于排查
cmake --build "$BUILD_DIR" -j "$JOBS" --target $BUILD_TARGETS

echo ">>> [4/4] 产物:"
for t in $BUILD_TARGETS; do
    if [ -f "$BUILD_DIR/$t" ]; then
        echo "    $BUILD_DIR/$t  ($(du -h "$BUILD_DIR/$t" | cut -f1))"
    else
        echo "    $BUILD_DIR/$t  (未生成)"
    fi
done
echo ">>> 部署: BOX_PASS=xxx bash wsl-cross/deploy-wsl.sh"
