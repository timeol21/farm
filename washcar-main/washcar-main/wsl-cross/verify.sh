#!/usr/bin/env bash
# =============================================================================
# 逻辑验证闸门: 一条命令跑完"编译验证 + 运行验证"
#
# 解决什么:
#   平时验代码逻辑的回路是"交叉编译 aarch64 → scp 上盒子 → 起进程 → 看日志",
#   一轮以分钟计,还占着现场设备。这里把闸门拆成四层,全部在 WSL 本机完成:
#
#   [1] 环境自检    工具链和原生依赖库齐不齐
#   [2] 语法闸门    全量源文件 g++ -fsyntax-only 并行过一遍 —— 最快的编译错误发现
#   [3] 原生编译    x86_64 编译出可执行文件(硬件库走桩,业务逻辑路径完全真实)
#   [4] 逻辑测试    逐个跑,收退出码,分档报告
#   [5] 梯形图闸门  tools/plc-mock 自检(可选)
#
# 用法(WSL 内,项目根目录):
#   bash wsl-cross/verify.sh              # 全跑(约 40s)
#   bash wsl-cross/verify.sh --fast       # 秒级档:跳过语法闸门(约 12s)
#   bash wsl-cross/verify.sh --quick      # 跳过语法闸门(已编译过、只改了一两行时)
#   bash wsl-cross/verify.sh --syntax-only
#   bash wsl-cross/verify.sh --no-mock    # 不跑梯形图闸门
#   bash wsl-cross/verify.sh --cross      # 追加 aarch64 交叉编译闸门(确认盒子产物没坏)
#   bash wsl-cross/verify.sh --e2e        # 追加海天梯形图闭环(约 30~60s, 需要原生 main)
#
# 退出码: 0 = 全过; 1 = 有 FAIL; 2 = 环境不满足; 3 = 用法错误
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WORK="${WSL_FAST_DIR:-${HOME}/washcar-wsl}"
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build-native}"
JOBS="${JOBS:-4}"
TEST_TIMEOUT="${TEST_TIMEOUT:-180}"

RUN_SYNTAX=1
RUN_BUILD=1
RUN_TESTS=1
RUN_MOCK=1
RUN_CROSS=0
RUN_E2E=0
ONLY_SYNTAX=0
FAST=0

for arg in "$@"; do
    case "$arg" in
        --quick)      RUN_SYNTAX=0 ;;
        --fast)       RUN_SYNTAX=0; FAST=1 ;;
        --syntax-only) ONLY_SYNTAX=1; RUN_BUILD=0; RUN_TESTS=0; RUN_MOCK=0 ;;
        --no-mock)    RUN_MOCK=0 ;;
        --cross)      RUN_CROSS=1 ;;
        --e2e)        RUN_E2E=1 ;;
        -h|--help)    sed -n '2,32p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "未知参数: $arg" >&2; exit 3 ;;
    esac
done

REPORT_DIR="${BUILD_DIR}/verify-reports"
mkdir -p "${REPORT_DIR}"

PASS_COUNT=0
FAIL_COUNT=0
NOTRUN_COUNT=0
FAILED_NAMES=()

hr()  { printf '%s\n' "------------------------------------------------------------"; }
hdr() { printf '\n=== %s ===\n' "$*"; }
ok()   { printf '  [PASS] %s\n' "$*"; PASS_COUNT=$((PASS_COUNT+1)); }
bad()  { printf '  [FAIL] %s\n' "$*"; FAIL_COUNT=$((FAIL_COUNT+1)); FAILED_NAMES+=("$*"); }
skip() { printf '  [NO-OP] %s\n' "$*"; NOTRUN_COUNT=$((NOTRUN_COUNT+1)); }

START_TS=$(date +%s)

# =============================================================================
hdr "0. 环境自检"
# =============================================================================
ENV_ERR=0
for cmd in g++ gcc cmake ninja ccache rsync python3; do
    if command -v "$cmd" >/dev/null 2>&1; then
        ok "$cmd $(command -v "$cmd" | sed 's|.*/||')"
    else
        bad "$cmd 不存在"
        ENV_ERR=1
    fi
done
# 原生构建要链的库
for hdr_file in /usr/include/openssl/ssl.h /usr/include/nlohmann/json.hpp; do
    if [ -f "$hdr_file" ]; then ok "头文件 ${hdr_file}"; else bad "缺 ${hdr_file}(原生构建链不过)"; ENV_ERR=1; fi
done
if ldconfig -p 2>/dev/null | grep -q "libavcodec.so"; then
    ok "ffmpeg 运行时库在"
else
    bad "缺 ffmpeg 运行库(libavcodec),原生构建链不过"
    ENV_ERR=1
fi
if [ "$ENV_ERR" -ne 0 ]; then
    hr
    echo "环境不满足,先修上面标 FAIL 的项(WSL 内: sudo apt install -y libssl-dev nlohmann-json3-dev libavcodec-dev libavformat-dev libswscale-dev libavutil-dev cmake ninja-build ccache)"
    exit 2
fi

# =============================================================================
# [2] 语法闸门: 全量源文件并行 -fsyntax-only
# 这一层不生成代码、不链接,专门用最短时间把"编译不过"揪出来。
# =============================================================================
if [ "$RUN_SYNTAX" -eq 1 ]; then
    hdr "1. 语法闸门(全量源文件, -fsyntax-only)"
    SYNC_T0=$SECONDS
    bash "${SCRIPT_DIR}/build-native.sh" --sync-only >/dev/null 2>&1
    echo "  源码同步到 WSL 原生盘 ($((SECONDS-SYNC_T0))s)"
    SYNTAX_T0=$SECONDS
    SYNTAX_LIST="${REPORT_DIR}/syntax-files.txt"
    # 从 src/CMakeLists.txt 里抽出所有参与编译的源文件(去重)。
    # 注释掉的行要跳过 —— 那些是历史遗留文件,根本不在任何 target 里,
    # 拿它们报"编不过"是误报(main.cpp / device_service_test.cpp 就是这种)。
    awk '
        {
            line = $0
            hash = index(line, "#")
            if (hash > 0) line = substr(line, 1, hash - 1)
            if (match(line, /[A-Za-z0-9_][A-Za-z0-9_\/.]*\.(cpp|cc|c)/)) {
                print substr(line, RSTART, RLENGTH)
            }
        }
    ' "${WORK}/src/CMakeLists.txt" | sort -u > "${SYNTAX_LIST}"
    TOTAL_SRC=$(wc -l < "${SYNTAX_LIST}")
    echo "  参与编译的源文件: ${TOTAL_SRC} 个"

    INC_ARGS=(-Iinclude -Isrc -Ithird_party/inih -Ilib/rknn/utils
              -Ilib/rknn/3rdparty/rknpu2/include -Iinclude/common/ai/yolov8
              -Ilib/rknn/3rdparty/librga/include -Ilib/rknn/3rdparty/stb_image
              -Ilib/rknn/3rdparty/jpeg_turbo/include
              -Ilib/rknn)

    # 用项目自己的编译器跑语法检查:gcc-9 交叉编译器 + 同一套 sysroot,
    # 和盒子构建完全同构(头文件、宏、方言都一样)。
    # 本机 gcc-11 更严(比如 vendor 头 font8x8_basic.h 里 char 数组装 0xFF 的窄化),
    # 用本机编译器会把"盒子构建能过"的文件误报成编不过 —— 闸门一旦有误报就没人信它了。
    SYNTAX_SYSROOT=""
    if command -v aarch64-linux-gnu-g++-9 >/dev/null 2>&1; then
        SYNTAX_CXX=aarch64-linux-gnu-g++-9
        SYNTAX_CC=aarch64-linux-gnu-gcc-9
        SYNTAX_COMPILER="aarch64-linux-gnu-g++-9"
        if [ -d /opt/rk3568-sysroot ]; then
            # 不带 sysroot 的话 ffmpeg 头找不到(它在 sysroot 的多架构子目录里)
            SYNTAX_SYSROOT="--sysroot=/opt/rk3568-sysroot"
            SYNTAX_COMPILER="${SYNTAX_COMPILER} + sysroot(与盒子构建同一套)"
        else
            SYNTAX_COMPILER="${SYNTAX_COMPILER}(没装 sysroot,ffmpeg 头可能找不到)"
        fi
    else
        SYNTAX_CXX=g++
        SYNTAX_CC=gcc
        SYNTAX_COMPILER="g++(本机,与盒子 gcc-9 有版本差异,可能误报)"
    fi
    echo "  语法检查编译器: ${SYNTAX_COMPILER}"

    SYNTAX_ERR_DIR="${REPORT_DIR}/syntax-err"
    rm -rf "${SYNTAX_ERR_DIR}"; mkdir -p "${SYNTAX_ERR_DIR}"
    export SYNTAX_ERR_DIR WORK SYNTAX_CXX SYNTAX_CC SYNTAX_SYSROOT
    export INC_ARGS_STR="${INC_ARGS[*]}"

    syntax_check_one() {
        rel="$1"
        case "$rel" in
            lib/*)  path="${WORK}/${rel}"; cxx="$SYNTAX_CC"; std=gnu11 ;;
            *.c)    path="${WORK}/src/${rel}"; cxx="$SYNTAX_CC"; std=gnu11 ;;
            *)      path="${WORK}/src/${rel}"; cxx="$SYNTAX_CXX"; std=c++17 ;;
        esac
        [ -f "$path" ] || { echo "MISSING $rel"; return 0; }
        err="${SYNTAX_ERR_DIR}/$(echo "$rel" | tr '/' '_').err"
        if $cxx -std=$std -fsyntax-only -DDISABLE_RGA ${SYNTAX_SYSROOT} ${INC_ARGS_STR} "$path" 2>"$err"; then
            return 0
        fi
        echo "FAIL $rel"
        return 1
    }
    export -f syntax_check_one

    SYNTAX_FAILS=$(cd "${WORK}" && xargs -a "${SYNTAX_LIST}" -P "${JOBS}" -n 1 \
        bash -c 'syntax_check_one "$1"' _ 2>/dev/null | tee "${REPORT_DIR}/syntax-fails.txt")
    SYNTAX_T1=$SECONDS
    # grep -c 没有匹配时输出 0 但退出码是 1,用 || true 兜住(不要 || echo 0,会变成两行 0)
    SYNTAX_BAD=$(grep -c '^FAIL ' "${REPORT_DIR}/syntax-fails.txt" 2>/dev/null || true)
    SYNTAX_BAD=${SYNTAX_BAD:-0}
    if [ "${SYNTAX_BAD}" -eq 0 ]; then
        ok "全量 ${TOTAL_SRC} 个源文件语法闸门通过 ($((SYNTAX_T1-SYNTAX_T0))s)"
    else
        bad "${SYNTAX_BAD}/${TOTAL_SRC} 个源文件编不过 ($((SYNTAX_T1-SYNTAX_T0))s)"
        grep '^FAIL ' "${REPORT_DIR}/syntax-fails.txt" | head -20 | while read -r _ f; do
            echo "        --- ${f} ---"
            sed -n '1,6p' "${SYNTAX_ERR_DIR}/$(echo "$f" | tr '/' '_').err" | sed 's/^/        /'
        done
        hr
        echo "语法闸门没过,先修编译错误(完整报错见 ${SYNTAX_ERR_DIR}/)"
        exit 1
    fi
fi

# =============================================================================
# [3] 原生编译
# =============================================================================
if [ "$RUN_BUILD" -eq 1 ]; then
    hdr "2. 原生编译(x86_64, 硬件库走桩)"
    T0=$(date +%s)
    if BUILD_TARGETS="${BUILD_TARGETS:-}" bash "${SCRIPT_DIR}/build-native.sh" > "${REPORT_DIR}/build.log" 2>&1; then
        T1=$(date +%s)
        ok "原生编译通过 (耗时 $((T1-T0))s)"
    else
        T1=$(date +%s)
        bad "原生编译失败 (耗时 $((T1-T0))s)"
        tail -n 40 "${REPORT_DIR}/build.log" | sed 's/^/        /'
        hr
        echo "编译失败,先修编译错误(完整日志 ${REPORT_DIR}/build.log)"
        exit 1
    fi
fi

# =============================================================================
# [4] 逻辑测试
# =============================================================================
if [ "$RUN_TESTS" -eq 1 ]; then
    hdr "3. 逻辑测试"
    TESTS_T0=$SECONDS
    # 纯逻辑 target:不碰硬件,秒级
    TESTS_LOGIC="config_test washcar_test command_test http_test timer_test"
    # 全链路 target:桩掉硬件库后业务链路仍是真实的。
    # 同步检测改造后它已经降到 1s 内, --fast 档也一起跑, 不再单独跳过。
    TESTS_FULL="detection_test"
    # 客户端工具,不是自跑测试(要连外部 PLC socket,命令行还得带参数)——
    # 混进自跑列表会一直报"退出码 2",那是用法错误不是回归。
    TOOLS_CLIENT="fx_test_client_socket"

    for t in ${TESTS_LOGIC} ${TESTS_FULL}; do
        BIN="${BUILD_DIR}/src/${t}"
        if [ ! -x "${BIN}" ]; then
            skip "${t} (没编出产物)"
            continue
        fi
        LOG="${REPORT_DIR}/${t}.log"
        T0=$SECONDS
        ( cd "${REPORT_DIR}" && timeout "${TEST_TIMEOUT}" "${BIN}" ) >"${LOG}" 2>&1
        RC=$?
        T1=$SECONDS
        if [ "${RC}" -eq 0 ]; then
            ok "$(printf '%-28s' "${t}") ($((T1-T0))s)"
        elif [ "${RC}" -eq 124 ]; then
            bad "$(printf '%-28s' "${t}") 超时 ${TEST_TIMEOUT}s"
        else
            bad "$(printf '%-28s' "${t}") 退出码 ${RC}"
            tail -n 15 "${LOG}" | sed 's/^/        /'
        fi
    done

    for t in ${TOOLS_CLIENT}; do
        if [ -x "${BUILD_DIR}/src/${t}" ]; then
            skip "$(printf '%-28s' "${t}") 客户端工具(需外部 PLC socket),非自跑测试"
        else
            skip "${t} (没编出产物)"
        fi
    done
    echo "  本阶段耗时 $((SECONDS-TESTS_T0))s"
fi

# =============================================================================
# [5] 梯形图模拟器闸门
# =============================================================================
if [ "$RUN_MOCK" -eq 1 ]; then
    hdr "4. 梯形图模拟器闸门(tools/plc-mock)"
    MOCK_DIR="${PROJECT_ROOT}/tools/plc-mock"
    [ -d "${MOCK_DIR}" ] || MOCK_DIR="${WORK}/tools/plc-mock"
    CSV=""
    if [ -d "${MOCK_DIR}" ]; then
        # 机型资料（含梯形图）在 docs/<机型>/*.csv；老布局 simulators/<机型>/data 也兼容。
        # 指定机型：PLC=xxx bash verify.sh（路径问 start.py 要）；不指定就取第一个。
        if [ -n "${PLC:-}" ]; then
            CSV=$(cd "${MOCK_DIR}" && python3 start.py --plc "${PLC}" --print-ladder 2>/dev/null)
            if [ -f "${CSV}" ]; then
                echo "  机型 ${PLC}: $(basename "${CSV}")"
            else
                echo "  ! 机型 ${PLC} 没有可用的梯形图，退回第一个"
                CSV=""
            fi
        fi
        if [ -z "${CSV}" ]; then
            for f in "${MOCK_DIR}"/docs/*/*.csv "${MOCK_DIR}"/simulators/*/data/*.csv "${MOCK_DIR}"/data/*.csv; do
                [ -f "$f" ] && CSV="$f" && break
            done
        fi
    fi
    if [ -z "${CSV}" ]; then
        skip "梯形图闸门(找不到 tools/plc-mock/simulators/*/data/*.csv)"
    else
        MOCK_T0=$SECONDS
        MLOG="${REPORT_DIR}/plc-mock.log"
        if ( cd "${MOCK_DIR}" && timeout "${TEST_TIMEOUT}" python3 tests/verify_mock.py "${CSV}" ) >"${MLOG}" 2>&1; then
            ok "梯形图模拟器自检通过 ($(basename "${CSV}"), $((SECONDS-MOCK_T0))s)"
        else
            bad "梯形图模拟器自检失败 ($((SECONDS-MOCK_T0))s)"
            tail -n 20 "${MLOG}" | sed 's/^/        /'
        fi
    fi
fi

# =============================================================================
# [可选] 海天端到端闭环: 梯形图虚拟 PLC + mini MQTT broker + 原生 main
# 覆盖"命令下发 -> 状态机 -> 写 PLC -> 上行报文 -> 洗完收尾"整条链路
# =============================================================================
if [ "$RUN_E2E" -eq 1 ]; then
    hdr "5. 海天端到端闭环(plc-mock)"
    if [ ! -x "${BUILD_DIR}/src/main" ]; then
        echo "  原生 main 不在, 先编一份(约 20s)"
        ( cd "${PROJECT_ROOT}" && BUILD_TARGETS="main" bash "${SCRIPT_DIR}/build-native.sh" ) \
            > "${REPORT_DIR}/build-main.log" 2>&1 || true
    fi
    if [ ! -x "${BUILD_DIR}/src/main" ]; then
        skip "端到端闭环(原生 main 编不出来, 见 ${REPORT_DIR}/build-main.log)"
    else
        E2E_LOG="${REPORT_DIR}/e2e-plcmock.log"
        E2E_RUN_DIR="${REPORT_DIR}/e2e-run"
        E2E_T0=$SECONDS
        ( cd "${PROJECT_ROOT}" && RUN_DIR="${E2E_RUN_DIR}" TIME_SCALE=50 \
          timeout 300 bash tools/plc-mock/run_local_native.sh ) \
            > "${E2E_LOG}" 2>&1
        E2E_RC=$?
        if [ "${E2E_RC}" -ne 0 ]; then
            bad "端到端闭环执行失败(rc=${E2E_RC})"
            tail -n 20 "${E2E_LOG}" | sed 's/^/        /'
        elif ! grep -q "工序数: 10" "${E2E_LOG}"; then
            bad "端到端闭环: D142 没走完 1~10"
            tail -n 20 "${E2E_LOG}" | sed 's/^/        /'
        elif ! grep -q "commandreply" "${E2E_RUN_DIR}/mqtt_broker.log" 2>/dev/null; then
            bad "端到端闭环: 没看到 commandreply 回执"
            tail -n 20 "${E2E_LOG}" | sed 's/^/        /'
        else
            ok "端到端闭环通过(D142 1~10 + 命令回执, $((SECONDS-E2E_T0))s)"
        fi
    fi
fi

# =============================================================================
# [可选] aarch64 交叉编译闸门:确认盒子产物没被改坏
# =============================================================================
if [ "$RUN_CROSS" -eq 1 ]; then
    hdr "6. aarch64 交叉编译闸门(盒子产物)"
    T0=$(date +%s)
    if BUILD_TARGETS="main" bash "${SCRIPT_DIR}/build-fast.sh" > "${REPORT_DIR}/build-cross.log" 2>&1; then
        T1=$(date +%s)
        ok "aarch64 main 交叉编译通过 (耗时 $((T1-T0))s)"
    else
        T1=$(date +%s)
        bad "aarch64 交叉编译失败 (耗时 $((T1-T0))s)"
        tail -n 30 "${REPORT_DIR}/build-cross.log" | sed 's/^/        /'
    fi
fi

# =============================================================================
hdr "汇总"
# =============================================================================
END_TS=$(date +%s)
printf '  通过 %d 项,失败 %d 项,未运行 %d 项,总耗时 %ds\n' \
    "${PASS_COUNT}" "${FAIL_COUNT}" "${NOTRUN_COUNT}" "$((END_TS-START_TS))"
if [ "${FAIL_COUNT}" -gt 0 ]; then
    printf '  失败项:\n'
    for n in "${FAILED_NAMES[@]}"; do printf '    - %s\n' "$n"; done
    hr
    echo " 结论: 未通过"
    exit 1
fi
hr
echo " 结论: 全部通过"
exit 0
