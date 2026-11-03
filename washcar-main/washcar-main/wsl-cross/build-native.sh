#!/usr/bin/env bash
# =============================================================================
# WSL 原生 x86_64 构建(只验逻辑,不产出盒子产物)
#
# 为什么需要这条路:
#   盒子产物是 aarch64。交叉编译出来只能拿到盒子上跑,改一行代码要走
#   "编译 → scp → 上盒子 → 起进程 → 看日志" 一整圈,反馈以分钟计。
#   但日常要验的东西(状态机 / 命令策略 / MQTT 协议 / 点位表 / 配置)全是
#   纯逻辑,不依赖 NPU、RGA、摄像头和现场 PLC。把它们编成 x86_64 本机
#   可执行文件,测试在本机跑,不出 WSL,几秒出结论。
#
# 硬件依赖怎么处理:
#   仓库里的 librknnrt.so / librga.so / libturbojpeg.a 只有 aarch64 版,
#   x86 上链不上。这里用 wsl-cross/native-stub/ 里的同签名空实现顶掉
#   (桩只影响 AI 推理和图像硬件加速这两条像素路径,业务逻辑路径完全真实)。
#
# 用法(WSL 内,项目根目录):
#   bash wsl-cross/build-native.sh                     # 编默认那组逻辑 target
#   BUILD_TARGETS="command_test" bash wsl-cross/build-native.sh
#   BUILD_TARGETS="main" bash wsl-cross/build-native.sh   # 编本机可跑的 main
#   bash wsl-cross/build-native.sh --sync-only         # 只同步不编译
#
# 产物: $HOME/washcar-build-native/src/<target>   (x86_64,本机直接跑)
#
# 注意:和 build-fast.sh 共用同一份 WSL 源码副本(~/washcar-wsl),
#       两边不要同时跑(会互相 rsync --delete)。
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WORK="${WSL_FAST_DIR:-${HOME}/washcar-wsl}"
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build-native}"
JOBS="${JOBS:-4}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

# 逻辑验证够用的 target:纯逻辑的先编,重的(全链路)后编。
# 不编 camera_test / mqtt_test / comand_test / tcp_plc_test / ai_load_test /
# yolov8_perf_test —— 它们分别要真摄像头、真 broker、真 PLC、NPU 模型。
DEFAULT_TARGETS="config_test washcar_test command_test detection_test http_test timer_test fx_test_client_socket"
BUILD_TARGETS="${BUILD_TARGETS:-${DEFAULT_TARGETS}}"
ACTION="${1:-build}"

# 源码指纹:只算参与编译的源文件 + 硬件桩。
# 同步后校验指纹,保证"编的就是当前这份"。
fingerprint() {
    ( cd "$1" && find src include lib/rknn/utils wsl-cross/native-stub -type f \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.cc' \) -print0 \
        | sort -z | xargs -0 md5sum | md5sum | cut -d' ' -f1 )
}

echo ">>> [1/4] 同步源码到原生盘: ${WORK}"
mkdir -p "${WORK}"
# 排除项和 build-fast.sh 保持一致(tools/ 29M,不参与编译,不搬)
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

echo ">>> [2/4] configure (原生 x86_64, BUILD_DIR=${BUILD_DIR})"
# -O0 -g0:逻辑验证不追性能,编译速度优先。默认 Release 的 -O3 会让编译慢 2~3 倍。
CACHE="${BUILD_DIR}/CMakeCache.txt"
if [ ! -f "${CACHE}" ] \
    || ! grep -qF "CMAKE_HOME_DIRECTORY:INTERNAL=${WORK}" "${CACHE}" \
    || ! grep -qF "WASHCAR_NATIVE:BOOL=ON" "${CACHE}" \
    || ! grep -qF "CMAKE_BUILD_TYPE:STRING=${BUILD_TYPE}" "${CACHE}"; then
    rm -f "${CACHE}"
    cmake -S "${WORK}" -B "${BUILD_DIR}" -G Ninja \
        -DWASHCAR_NATIVE=ON \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_C_FLAGS_RELEASE="-O0 -g0 -pipe" \
        -DCMAKE_CXX_FLAGS_RELEASE="-O0 -g0 -pipe" \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        >/dev/null
else
    echo "    缓存有效, 跳过 configure"
fi

echo ">>> [3/4] build: ${BUILD_TARGETS} (JOBS=${JOBS})"
LOG="${BUILD_DIR}/build-native.log"
if ! cmake --build "${BUILD_DIR}" -j "${JOBS}" --target ${BUILD_TARGETS} >"${LOG}" 2>&1; then
    echo ">>> BUILD FAILED. 最后 120 行:" >&2
    tail -n 120 "${LOG}" >&2
    echo ">>> 完整日志: ${LOG}" >&2
    exit 1
fi
tail -n 20 "${LOG}"

echo ">>> [4/4] 产物:"
FAILED=0
for t in ${BUILD_TARGETS}; do
    BIN="${BUILD_DIR}/src/${t}"
    if [ ! -f "${BIN}" ]; then
        echo "ERROR: target ${t} 没有产物 (${BIN})" >&2
        FAILED=1
        continue
    fi
    file "${BIN}" | grep -q "x86-64" || {
        echo "ERROR: ${BIN} 不是 x86_64 产物" >&2
        file "${BIN}" >&2
        FAILED=1
        continue
    }
    echo "    ARTIFACT=${BIN} ($(du -h "${BIN}" | cut -f1))"
done
[ "$FAILED" -eq 0 ] || exit 4
echo ">>> 跑测试: bash wsl-cross/verify.sh"
