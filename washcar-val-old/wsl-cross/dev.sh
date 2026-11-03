#!/usr/bin/env bash
# =============================================================================
# 日常开发闭环：改代码 → 编译 → 跑测试 / 跑 main
#
# 在 WSL 里，项目根目录下执行：
#   bash wsl-cross/dev.sh build    # 只编 main（增量，平时最常用）
#   bash wsl-cross/dev.sh test     # 编 4 个测试 target 并全部跑一遍
#   bash wsl-cross/dev.sh run      # 编 main 并用 qemu 跑起来看启动日志
#   bash wsl-cross/dev.sh all      # build + test
#   bash wsl-cross/dev.sh clean    # 删掉构建目录
#
# 和 build-wsl.sh 的分工：
#   build-wsl.sh  是"完整构建工具"，负责 configure/rebuild/多 target/部署，参数多
#   dev.sh        是"日常快捷键"，只做你每天都在重复的三件事，参数就四个
#
# 环境变量（一般不用改）：
#   BUILD_DIR=<path>   构建目录，默认 $HOME/washcar-build（必须放 Linux 原生盘）
#   SYSROOT=<path>     sysroot，默认 /opt/rk3568-sysroot
#   JOBS=<n>           并行度，默认 4
#   QEMU=<cmd>         qemu 命令，默认 qemu-aarch64-static
#   MAIN_TIMEOUT=<s>   run 跑多久自动停，默认 20 秒
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
SYSROOT="${SYSROOT:-/opt/rk3568-sysroot}"
JOBS="${JOBS:-4}"
QEMU="${QEMU:-qemu-aarch64-static}"
MAIN_TIMEOUT="${MAIN_TIMEOUT:-20}"

TEST_TARGETS="config_manager_test default_point_test washcar_state_machine_test mqtt_outbox_test"

# main 运行时依赖的两个厂商库（不在 sysroot 里，在仓库的 lib/ 目录）
RKNN_LIB_DIRS="${PROJECT_ROOT}/lib/rknn/3rdparty/librga/Linux/aarch64:${PROJECT_ROOT}/lib/rknn/3rdparty/rknpu2/Linux/aarch64"

ACTION="${1:-build}"

say() { echo ""; echo ">>> $*"; }
die() { echo ""; echo "[ERROR] $*" >&2; exit 1; }

# ---------- 前置检查（只查一次，避免跑了半天才发现环境没搭） ----------
preflight() {
    command -v aarch64-linux-gnu-g++-9 >/dev/null 2>&1 \
        || die "找不到 aarch64-linux-gnu-g++-9。先跑一次: sudo bash wsl-cross/setup-env.sh"
    [ -d "$SYSROOT" ] \
        || die "找不到 $SYSROOT。先跑一次: sudo bash wsl-cross/setup-env.sh"
    # 构建目录放在 /mnt/* 上会慢一个数量级，这里直接拦住
    case "$BUILD_DIR" in
        /mnt/*) die "BUILD_DIR=$BUILD_DIR 在 /mnt 上，编译会非常慢。请换成 Linux 原生盘，例如 \$HOME/washcar-build" ;;
    esac
}

# ---------- 编译 ----------
do_build() {
    local targets="$1"
    preflight
    say "编译: ${targets}"
    BUILD_TARGETS="$targets" JOBS="$JOBS" BUILD_DIR="$BUILD_DIR" \
        bash "${SCRIPT_DIR}/build-wsl.sh" || die "编译失败，往上翻找第一个 error"
}

# ---------- 跑测试 ----------
do_test() {
    command -v "$QEMU" >/dev/null 2>&1 \
        || die "找不到 $QEMU。装一下: sudo apt-get install -y qemu-user-static"

    local failed=0 passed=0
    for t in $TEST_TARGETS; do
        local bin="${BUILD_DIR}/${t}"
        [ -x "$bin" ] || { echo "  [SKIP] $t 不存在"; failed=$((failed+1)); continue; }
        # 测试要在项目根目录跑，有些用例会读写相对路径下的配置
        local out
        out=$(cd "$PROJECT_ROOT" && timeout 180 "$QEMU" -L "$SYSROOT" "$bin" 2>&1 | tail -1)
        if echo "$out" | grep -qE "0 failed"; then
            printf "  [PASS] %-28s %s\n" "$t" "$out"
            passed=$((passed+1))
        else
            printf "  [FAIL] %-28s %s\n" "$t" "${out:-<无输出>}"
            failed=$((failed+1))
        fi
    done

    say "测试结果: ${passed} 个 target 通过, ${failed} 个失败"
    [ "$failed" -eq 0 ] || exit 1
}

# ---------- 跑 main ----------
do_run() {
    command -v "$QEMU" >/dev/null 2>&1 \
        || die "找不到 $QEMU。装一下: sudo apt-get install -y qemu-user-static"
    local bin="${BUILD_DIR}/main"
    [ -x "$bin" ] || die "$bin 不存在，先跑: bash wsl-cross/dev.sh build"

    say "启动 main（${MAIN_TIMEOUT}s 后自动停止）"
    echo "    停下来的地方是缺硬件，属于正常现象："
    echo "      /tmp/vplc-pty 不存在  -> 海天 FX 串口在 WSL 里没有"
    echo "      RTSP 连不上           -> 摄像头不在同一网络"
    echo "      yolov8/plate 加载失败 -> rknn 模型文件不在 WSL 里"
    echo ""

    # qemu 模拟运行时的工作目录设成项目根，和盒子上 ./data 相对路径保持一致
    ( cd "$PROJECT_ROOT" && \
      LD_LIBRARY_PATH="$RKNN_LIB_DIRS" \
      timeout "$MAIN_TIMEOUT" "$QEMU" -L "$SYSROOT" "$bin" 2>&1 | \
      grep -vE "^\s*$" | head -60 ) || true

    echo ""
    say "main 已停止（timeout 主动终止，属正常）"
}

# ---------- 清理 ----------
do_clean() {
    say "删除构建目录: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    echo "  已删除"
}

# ---------- 入口 ----------
case "$ACTION" in
    build) do_build "main" ;;
    test)  do_build "$TEST_TARGETS"; do_test ;;
    run)   do_build "main"; do_run ;;
    all)   do_build "main $TEST_TARGETS"; do_test; do_run ;;
    clean) do_clean ;;
    *)
        echo "用法: bash wsl-cross/dev.sh [build|test|run|all|clean]"
        echo ""
        echo "  build  只编 main（增量，约 4 秒）"
        echo "  test   编 4 个测试 target 并全部跑一遍"
        echo "  run    编 main 并用 qemu 跑起来看启动日志"
        echo "  all    build + test"
        echo "  clean  删掉构建目录"
        exit 1
        ;;
esac
