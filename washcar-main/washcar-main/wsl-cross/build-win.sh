#!/usr/bin/env bash
# =============================================================================
# 一站式构建引擎 —— Windows 一键菜单(wsl-cross/build-menu.ps1)的后端
#
# 谁在调它:  wsl-cross/一键编译.cmd  →  build-menu.ps1  →  wsl bash build-win.sh
#            也可以直接在 WSL 里跑(见下面"用法")。
#
# 它干的事(一条龙):
#   环境体检 → 同步源码到 WSL 原生盘 → configure → 编译(实时吐进度)
#            → 收集产物 → 回传到 Windows 的 wsl-cross/dist/
#
# 为什么不用现成的 build-fast.sh / build-native.sh:
#   那两个把 cmake 输出重定向进日志、失败才 tail 120 行 —— 交互式编译时屏幕上什么都没有。
#   本脚本改成"边编边吐标记",由 PowerShell 渲染成进度条 + 实时报错 + 失败摘要。
#   同步这一步仍然复用 build-fast.sh --sync-only,rsync 排除规则只维护一份,不会跑偏。
#
# 为什么产物要另存一份到 Windows:
#   盒子现场调试时往往是"把 U 盘/共享目录里的 main 拷过去",而不是从 WSL scp。
#   dist/ 就是给手工拷贝用的落点,顺带把 librknnrt.so / librga.so 和校验清单一齐备好。
#
# 标记协议(每行一条,PS 按前缀识别;不带标记的行原样显示):
#   @@PHASE@@  <n>/<total> <名称>
#   @@PROG@@   <cur> <total> <描述>     编译进度(ninja 的 [x/y])
#   @@ERR@@    <文本>                   错误(红色)
#   @@ERRCTX@@ <文本>                   错误的源码上下文(红色缩进)
#   @@WARN@@   <文本>                   警告(黄色)
#   @@NOTE@@   <文本>                   普通提示(灰)
#   @@HINT@@   <文本>                   修复建议(青)
#   @@TAIL@@   <文本>                   失败时的日志尾巴
#   @@ART@@    <路径>|<大小>|<标签>      产物
#   @@INFO@@   key=value                给 PS 用的键值(LOG= / DIST= / COMMIT=)
#   @@UPTODATE@@                        ninja 说没活可干
#   @@END@@    <ok|fail> <秒数> <产物数>
#
# 用法(WSL 内,项目根目录):
#   bash wsl-cross/build-win.sh --mode aarch64 --targets main
#   bash wsl-cross/build-win.sh --mode native  --targets "main config_test"
#   bash wsl-cross/build-win.sh --mode aarch64 --targets all --build-type Debug
#   bash wsl-cross/build-win.sh --check                       # 只体检环境
#   bash wsl-cross/build-win.sh --list-targets                # 列出从 CMakeLists 发现的目标
#   bash wsl-cross/build-win.sh --action run --target-name config_test
#   bash wsl-cross/build-win.sh --mode aarch64 --rebuild       # 清构建目录重来
#
# 退出码: 0 成功 / 1 编译失败 / 2 环境不满足 / 3 用法错误
# =============================================================================
set -uo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd)"
PROJECT_ROOT="${WC_PROJECT_ROOT:-$(cd -P "${SCRIPT_DIR}/.." && pwd)}"

MODE="${WC_MODE:-aarch64}"
TARGETS_RAW="${WC_TARGETS:-main}"
BUILD_TYPE="${WC_BUILD_TYPE:-Release}"
ACTION="${WC_ACTION:-build}"
# 0 = 按 CPU 核数自动(默认);显式给数字就按给的来
JOBS="${WC_JOBS:-0}"
DIST="${WC_DIST:-${PROJECT_ROOT}/wsl-cross/dist}"
DIST="${DIST%/}"    # 容忍结尾多写的斜杠,否则下面的约定校验会误判
VERBOSE="${WC_VERBOSE:-0}"
TARGET_NAME="${WC_TARGET_NAME:-}"
RUN_TIMEOUT="${WC_RUN_TIMEOUT:-300}"
# 交叉工具链的 sysroot:默认 /opt/rk3568-sysroot(setup-env.sh 装在这儿),
# 装到别处就设 WC_SYSROOT,不用改这个脚本和 toolchain 文件
SYSROOT="${WC_SYSROOT:-/opt/rk3568-sysroot}"
# 部署目标(写进"部署说明.txt"),格式 user@host:/路径;换机器/换账号设 WC_DEPLOY_TARGET
DEPLOY_TARGET="${WC_DEPLOY_TARGET:-ztl@192.168.31.248:~/washcar/deployed/}"
DO_SYNC=1
DO_REBUILD=0

# ---------- 参数解析(命令行优先级高于环境变量,方便在 WSL 里手动用) ----------
while [ $# -gt 0 ]; do
    case "$1" in
        --mode)        MODE="${2:-}"; shift 2 ;;
        --targets)     TARGETS_RAW="${2:-}"; shift 2 ;;
        --build-type)  BUILD_TYPE="${2:-}"; shift 2 ;;
        --action)      ACTION="${2:-}"; shift 2 ;;
        --target-name) TARGET_NAME="${2:-}"; shift 2 ;;
        --jobs|-j)     JOBS="${2:-0}"; shift 2 ;;
        --dist)        DIST="${2:-}"; shift 2 ;;
        --check)       ACTION=check; shift ;;
        --list-targets) ACTION=list; shift ;;
        --rebuild)     DO_REBUILD=1; shift ;;
        --no-sync)     DO_SYNC=0; shift ;;
        --verbose|-v)  VERBOSE=1; shift ;;
        -h|--help)     sed -n '2,48p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf '未知参数: %s\n' "$1" >&2; exit 3 ;;
    esac
done

case "$MODE" in native|aarch64) ;; *) printf 'ERROR: --mode 只能是 native / aarch64\n' >&2; exit 3 ;; esac
case "$BUILD_TYPE" in Release|Debug) ;; *) printf 'ERROR: --build-type 只能是 Release / Debug\n' >&2; exit 3 ;; esac
case "$ACTION" in build|check|run|gdb|clean|list|config) ;; *) printf 'ERROR: 未知 action: %s\n' "$ACTION" >&2; exit 3 ;; esac

# -j 0 或没给 → 按这台机器的核数自动定,避免"别人 16 核还只跑 4 个"
if [ "${JOBS}" -le 0 ] 2>/dev/null; then
    JOBS="$(nproc 2>/dev/null || echo 4)"
fi

# ---------- 输出标记(所有屏幕输出都走这几个函数,PS 才认得) ----------
T0=$SECONDS
ph()   { printf '@@PHASE@@ %s\n' "$*"; }
note() { printf '@@NOTE@@ %s\n' "$*"; }
warn() { printf '@@WARN@@ %s\n' "$*"; }
err()  { printf '@@ERR@@ %s\n' "$*"; }
hint() { printf '@@HINT@@ %s\n' "$*"; }
art()  { printf '@@ART@@ %s\n' "$*"; }
info() { printf '@@INFO@@ %s\n' "$*"; }
elapsed() { printf '%s' "$((SECONDS - T0))"; }
die() { local code="$1"; shift; err "$*"; printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit "$code"; }

# ---------- 构建目录 / 源码副本(与 build-fast.sh、build-native.sh 完全一致) ----------
# 共用同一个源码副本和 ccache,否则两套缓存互相作废,每次都要全量重编。
WORK="${WSL_FAST_DIR:-${HOME}/washcar-wsl}"
if [ "$MODE" = native ]; then
    BUILD_DIR="${WC_BUILD_DIR:-${HOME}/washcar-build-native}"
    SUBDIR="native"
    LOG="${BUILD_DIR}/build-native.log"
else
    BUILD_DIR="${WC_BUILD_DIR:-${HOME}/washcar-build}"
    SUBDIR="aarch64"
    LOG="${BUILD_DIR}/build-fast.log"
fi
[ "$BUILD_TYPE" = Debug ] && [ "$MODE" = aarch64 ] && SUBDIR="aarch64-debug"
DISTDIR="${DIST}/${SUBDIR}"

# 直跑 main 用的配置文件: 默认放在仓库里的 wsl-cross/run/config.json ——
# 这样 Windows 侧用 VS Code 直接改、能 diff、能提交; WC_CONFIG 可指向别处。
CONFIG_PATH="${WC_CONFIG:-${PROJECT_ROOT}/wsl-cross/run/config.json}"

export CCACHE_DIR="${CCACHE_DIR:-${HOME}/.ccache-wsl}"
export CCACHE_MAXSIZE=10G
export CCACHE_SLOPPINESS=time_macros,file_macro,include_file_mtime,include_file_ctime,system_headers

# ---------- 目标清单:从 src/CMakeLists.txt 自动发现 ----------
# 不写死清单:新增一个 add_executable(foo_test ...) 之后,菜单里要能自动出现,
# 否则每次加测试都得回来改脚本(而且要改 PS 和 bash 两份,迟早漏)。
# 说明文字也不用另写:直接取 CMakeLists 里已有的 "# foo_test: 一句话说明" 注释。
CMAKE_LISTS="${PROJECT_ROOT}/src/CMakeLists.txt"

discover_targets() {
    # 认识两种登记方式: CMake 原生的 add_executable(...) 和项目自己的 washcar_test_target(...)
    # 放宽了几种写法: 大小写(CMake 命令不区分)、命令和括号之间可以有空格
    grep -oiE '^[[:space:]]*(add_executable|washcar_test_target)[[:space:]]*\([[:space:]]*[A-Za-z0-9_]+' "$CMAKE_LISTS" 2>/dev/null \
        | sed -E 's/.*\([[:space:]]*//' | awk '!seen[$0]++'
}

target_desc() {
    # 说明文字取 target 上方那行注释, 分隔符允许 ":" "：" "──"
    local name="$1" line
    line=$(grep -m1 -E "^#[[:space:]]*${name}[[:space:]]*([:：]|──)" "$CMAKE_LISTS" 2>/dev/null) || return 0
    printf '%s' "$line" | sed -E "s/^#[[:space:]]*${name}[[:space:]]*([:：]|──)[[:space:]]*//"
}

DISCOVERED="$(discover_targets)"
[ -n "${DISCOVERED}" ] || {
    err "从 ${CMAKE_LISTS} 里没解析出任何 add_executable"
    hint "确认 src/CMakeLists.txt 存在且格式没大改(脚本按 'add_executable(name' 和 '# name: 说明' 两处匹配)"
    exit 2
}

expand_targets() {
    local out=" " t x
    for t in $1; do
        case "$t" in
            all) t="$DISCOVERED" ;;
        esac
        for x in $t; do
            case "$out" in *" $x "*) continue ;; esac
            out="${out}${x} "
        done
    done
    printf '%s' "$out"
}
TARGETS="$(expand_targets "$TARGETS_RAW")"
# 展示用(去掉首尾空格、压缩连续空格),免得清单里出现 "targets :  main"
TARGETS_DISPLAY="$(printf '%s' "$TARGETS" | sed 's/^ *//; s/ *$//; s/  */ /g')"

[ -n "${TARGETS// /}" ] || die 3 "目标为空"

CC_DESC="gcc $(gcc -dumpversion 2>/dev/null || echo '?') (宿主 x86_64)"
[ "$MODE" = aarch64 ] && CC_DESC="gcc-9 (aarch64-linux-gnu, sysroot ${SYSROOT}, 对齐盒子 glibc 2.31)"

# =============================================================================
# 环境体检 —— 少什么直接说清楚怎么装,不要让用户对着 cmake 报错猜
# =============================================================================
check_env() {
    local miss=0 c f
    ph "1/5 环境体检"
    note "构建方式: $([ "$MODE" = native ] && echo '原生 x86_64(WSL 内直接跑)' || echo 'RK3568 aarch64(盒子现场)')"

    for c in cmake ninja rsync ccache file; do
        if command -v "$c" >/dev/null 2>&1; then
            note "  ok  $c"
        else
            err "  缺命令: $c"
            miss=1
        fi
    done

    if [ "$MODE" = aarch64 ]; then
        for c in aarch64-linux-gnu-gcc-9 aarch64-linux-gnu-g++-9 aarch64-linux-gnu-strip; do
            if command -v "$c" >/dev/null 2>&1; then
                note "  ok  $c"
            else
                err "  缺交叉编译器: $c"
                miss=1
            fi
        done
        if [ -d "${SYSROOT}" ]; then
            note "  ok  sysroot ${SYSROOT}"
        else
            err "  缺 sysroot: ${SYSROOT}"
            hint "  WSL 里执行一次: sudo bash wsl-cross/setup-env.sh"
            hint "  (装在别处的话,设 WC_SYSROOT=/your/sysroot 再跑)"
            miss=1
        fi
    else
        for c in g++ gcc; do
            if command -v "$c" >/dev/null 2>&1; then
                note "  ok  $c"
            else
                err "  缺编译器: $c"
                miss=1
            fi
        done
        for f in /usr/include/openssl/ssl.h /usr/include/nlohmann/json.hpp; do
            [ -f "$f" ] && note "  ok  $f" || { err "  缺头文件: $f"; miss=1; }
        done
        if ldconfig -p 2>/dev/null | grep -q "libavcodec.so"; then
            note "  ok  ffmpeg 运行库"
        else
            err "  缺 ffmpeg 运行库(libavcodec)"
            miss=1
        fi
    fi

    # 这几个缺了也能编,只是功能降级,单独提示不判失败
    for c in stdbuf flock python3; do
        command -v "$c" >/dev/null 2>&1 || note "  可选工具缺失: ${c}$([ "$c" = stdbuf ] && echo '(没有它进度不实时)' || ([ "$c" = flock ] && echo '(没有它不防并发)' || echo '(直跑 main / mock 要用)'))"
    done

    if [ "$miss" -ne 0 ]; then
        hint "  一次性补齐(在 WSL 内):"
        hint "    sudo apt install -y build-essential cmake ninja-build ccache rsync file \\"
        hint "        libssl-dev nlohmann-json3-dev libavcodec-dev libavformat-dev libswscale-dev libavutil-dev"
        [ "$MODE" = aarch64 ] && hint "    sudo bash wsl-cross/setup-env.sh     # 交叉工具链 + sysroot"
        return 2
    fi
    note "  环境 OK"
    return 0
}

# =============================================================================
# 同步源码 —— 直接复用 build-fast.sh --sync-only(排除规则只维护一份)
# =============================================================================
do_sync() {
    ph "2/5 同步源码到 WSL 原生盘"
    note "  Windows(9p,慢) → ${WORK}(WSL 原生盘,快)"
    local sync_log="${BUILD_DIR}/sync.log"
    mkdir -p "${BUILD_DIR}"
    if ! bash "${PROJECT_ROOT}/wsl-cross/build-fast.sh" --sync-only >"${sync_log}" 2>&1; then
        err "  源码同步失败"
        tail -n 15 "${sync_log}" 2>/dev/null | while IFS= read -r l; do printf '@@ERRCTX@@ %s\n' "$l"; done
        return 1
    fi
    note "  完成(已校验源码指纹,编的就是当前这份)"
    return 0
}

# =============================================================================
# configure —— 缓存有效就跳过;参数变了/工具链更新了就重来
# =============================================================================
cache_ok() {
    local c="${BUILD_DIR}/CMakeCache.txt"
    [ -f "$c" ] || return 1
    # 改了 CMakeLists(比如新加/按模式排除 target)必须重配,否则 ninja 里的 target 名单是旧的
    [ "${WORK}/src/CMakeLists.txt" -nt "$c" ] && return 1
    grep -qF "CMAKE_HOME_DIRECTORY:INTERNAL=${WORK}" "$c" || return 1
    grep -qF "CMAKE_BUILD_TYPE:STRING=${BUILD_TYPE}" "$c" || return 1
    if [ "$MODE" = native ]; then
        grep -qF "WASHCAR_NATIVE:BOOL=ON" "$c" || return 1
    else
        grep -qF "CMAKE_TOOLCHAIN_FILE:FILEPATH=${WORK}/wsl-cross/toolchain-aarch64-wsl.cmake" "$c" || return 1
        # 改了 toolchain 必须全量重配:CMAKE_EXE_LINKER_FLAGS 是 CACHE 变量,不清不会生效
        [ "${WORK}/wsl-cross/toolchain-aarch64-wsl.cmake" -nt "$c" ] && return 1
        # sysroot 换了(WC_SYSROOT)也要重配,否则缓存的头文件/库路径还是老的
        [ "$(cat "${BUILD_DIR}/.sysroot" 2>/dev/null)" = "${SYSROOT}" ] || return 1
    fi
    return 0
}

# 这个构建目录里实际存在哪些可执行 target —— configure 之后才准。
# 菜单是按 CMakeLists 文本解析出来的(超集),这里以构建系统为准做交集:
# CMakeLists 里 if(NOT WASHCAR_NATIVE) 排掉的 target,原生构建下就查不到。
existing_targets() {
    ninja -C "${BUILD_DIR}" -t targets all 2>/dev/null \
        | grep 'CXX_EXECUTABLE_LINKER' \
        | sed -E 's/:.*//; s#^src/##' | sort -u
}

prune_missing_targets() {
    local avail keep="" missing="" t
    avail="$(existing_targets)"
    if [ -z "${avail}" ]; then
        warn "拿不到 ${BUILD_DIR} 的 target 清单,跳过这次校验"
        return 0
    fi
    for t in ${TARGETS}; do
        case $'\n'"${avail}"$'\n' in
            *$'\n'"${t}"$'\n'*) keep="${keep}${t} " ;;
            *) missing="${missing}${t} " ;;
        esac
    done
    if [ -n "${missing}" ]; then
        warn "这些 target 在当前构建里不存在,已跳过:${missing}"
        note "  src/CMakeLists.txt 里按构建模式排除了(见 WASHCAR_NATIVE 那段);换个方式编或者去掉排除"
    fi
    TARGETS="${keep}"
    TARGETS_DISPLAY="$(printf '%s' "${TARGETS}" | sed 's/^ *//; s/ *$//; s/  */ /g')"
    if [ -z "${TARGETS// /}" ]; then
        err "过滤后没有可编的目标"
        return 1
    fi
    return 0
}

do_configure() {
    ph "3/5 configure"
    local conf_log="${BUILD_DIR}/configure.log"
    mkdir -p "${BUILD_DIR}"

    if [ "$DO_REBUILD" -eq 1 ]; then
        note "  --rebuild:清空 ${BUILD_DIR}"
        rm -rf "${BUILD_DIR}"
        mkdir -p "${BUILD_DIR}"
    elif cache_ok; then
        note "  缓存有效,跳过 configure"
        return 0
    fi

    rm -f "${BUILD_DIR}/CMakeCache.txt"
    local args=(-S "${WORK}" -B "${BUILD_DIR}" -G Ninja
                -DCMAKE_BUILD_TYPE="${BUILD_TYPE}")

    if [ "$MODE" = native ]; then
        args+=(-DWASHCAR_NATIVE=ON
               -DCMAKE_C_COMPILER_LAUNCHER=ccache
               -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
        # 逻辑验证不追性能,-O0 -g0 编译最快;Debug 档保留 -g 供 gdb
        if [ "$BUILD_TYPE" = Release ]; then
            args+=(-DCMAKE_C_FLAGS_RELEASE="-O0 -g0 -pipe"
                   -DCMAKE_CXX_FLAGS_RELEASE="-O0 -g0 -pipe")
        else
            args+=(-DCMAKE_C_FLAGS_DEBUG="-O0 -g -pipe"
                   -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g -pipe")
        fi
    else
        args+=(-DCMAKE_TOOLCHAIN_FILE="${WORK}/wsl-cross/toolchain-aarch64-wsl.cmake")
        # Debug 档保留完整符号,方便盒子现场 gdb / addr2line
        [ "$BUILD_TYPE" = Debug ] && args+=(-DCMAKE_C_FLAGS_DEBUG="-O0 -g -pipe"
                                            -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g -pipe")
    fi

    note "  cmake -G Ninja  (BUILD_TYPE=${BUILD_TYPE})"
    if ! cmake "${args[@]}" >"${conf_log}" 2>&1; then
        err "  configure 失败"
        tail -n 25 "${conf_log}" 2>/dev/null | while IFS= read -r l; do printf '@@ERRCTX@@ %s\n' "$l"; done
        hint "  改了 CMakeLists / toolchain 后仍失败,可试 --rebuild 全量重来"
        return 1
    fi
    note "  configure 完成"
    [ "$MODE" = aarch64 ] && printf '%s' "${SYSROOT}" > "${BUILD_DIR}/.sysroot"
    return 0
}

# =============================================================================
# 编译 —— 边编边吐进度。stdbuf 强制行缓冲,否则管道里会攒一大块才出来
# =============================================================================
do_build() {
    # 目标多的时候标题只报个数,免得把整行撑爆
    local n_t=0 t title
    for t in ${TARGETS}; do n_t=$((n_t + 1)); done
    if [ "$n_t" -le 3 ]; then title="${TARGETS_DISPLAY}"; else title="${n_t} 个目标"; fi
    ph "4/5 编译 (${title} · -j ${JOBS})"
    : >"${LOG}"
    local num tot desc ctx=0 line

    # shellcheck disable=SC2086
    local -a runner=()
    command -v stdbuf >/dev/null 2>&1 && runner=(stdbuf -oL -eL)
    ${runner[@]+"${runner[@]}"} cmake --build "${BUILD_DIR}" -j "${JOBS}" --target ${TARGETS} 2>&1 \
    | while IFS= read -r line; do
        line="${line%$'\r'}"
        printf '%s\n' "$line" >>"${LOG}"
        case "$line" in
            \[*/*\]*)
                tot="${line#\[}"; num="${tot%%/*}"; tot="${tot#*/}"; tot="${tot%%\]*}"
                desc="${line#*\] }"; desc="${desc# }"
                printf '@@PROG@@ %s %s %s\n' "${num:-0}" "${tot:-0}" "$desc"
                # gcc 报错前面那行 FAILED 命令很长,不显示源码上下文
                ctx=0
                ;;
            "ninja: no work to do."*)
                printf '@@UPTODATE@@\n'
                ;;
            *" error:"*|*": error:"*|*"fatal error:"*|*"undefined reference"*|"FAILED:"*|"ninja: build stopped"*)
                printf '@@ERR@@ %s\n' "$line"
                ctx=2
                ;;
            *"warning:"*)
                printf '@@WARN@@ %s\n' "$line"
                ctx=0
                ;;
            *)
                if [ "$ctx" -gt 0 ]; then
                    printf '@@ERRCTX@@ %s\n' "$line"
                    ctx=$((ctx - 1))
                elif [ "$VERBOSE" = 1 ]; then
                    printf '@@NOTE@@   %s\n' "$line"
                fi
                ;;
        esac
    done
    local rc=${PIPESTATUS[0]}

    if [ "$rc" -ne 0 ]; then
        # pipefail + 子 shell 收不到 while 里的状态,用日志再兜一层判断
        return 1
    fi
    return 0
}

# 失败摘要:错误行 + 位置 + 日志尾巴 + 常见原因的修复建议
report_failure() {
    local n first
    n=$(grep -cE ': (error|fatal error):|undefined reference|^FAILED:' "${LOG}" 2>/dev/null || true)
    n="${n:-0}"
    err "编译失败:匹配到 ${n} 条错误"

    first=$(grep -m1 -E '^[^ :]+:[0-9]+:[0-9]+: (error|fatal error):' "${LOG}" 2>/dev/null || true)
    [ -n "$first" ] && err "首个错误: ${first}"

    note "---- 日志最后 30 行 ----"
    tail -n 30 "${LOG}" 2>/dev/null | while IFS= read -r l; do printf '@@TAIL@@ %s\n' "$l"; done

    # 常见坑:直接给修复动作,少走弯路
    if grep -q "nlohmann/json.hpp" "${LOG}" 2>/dev/null; then
        hint "缺 nlohmann/json.hpp:重跑 sudo bash wsl-cross/setup-env.sh"
    fi
    if grep -qE "GLIBC_2\.3[2-9]|pthread_create@GLIBC" "${LOG}" 2>/dev/null; then
        hint "混进了 gcc-11 的 libstdc++:确认 toolchain 里是 aarch64-linux-gnu-gcc-9,且 sysroot 有 libstdc++.so"
    fi
    if grep -q "fatal error: .*: No such file or directory" "${LOG}" 2>/dev/null; then
        hint "头文件找不到:先确认 sysroot 完整(${SYSROOT}),或检查 CMakeLists 的 include 路径"
    fi
    hint "完整日志: ${LOG}"
}

# =============================================================================
# 收集产物 + 回传到 Windows
# =============================================================================
prepare_dist() {
    case "${DIST}" in
        */wsl-cross/dist) ;;
        *)
            err "回传目录不符合约定(*/wsl-cross/dist),为安全起见拒绝清理: ${DIST}"
            return 1
            ;;
    esac
    rm -rf "${DISTDIR}"
    mkdir -p "${DISTDIR}" "${DIST}/logs" || return 1
    return 0
}

collect_artifacts() {
    ph "5/5 收集产物并回传到 Windows"
    prepare_dist || return 1

    local t src dst size f count=0 arch_re
    [ "$MODE" = native ] && arch_re="x86-64" || arch_re="ARM aarch64"

    for t in ${TARGETS}; do
        src="${BUILD_DIR}/src/${t}"
        if [ ! -f "${src}" ]; then
            err "  target 没有产物: ${t}  (${src})"
            continue
        fi
        if ! file "${src}" | grep -q "${arch_re}"; then
            err "  架构不对: ${t} 不是 ${arch_re}"
            file "${src}" | while IFS= read -r l; do printf '@@ERRCTX@@ %s\n' "$l"; done
            continue
        fi

        dst="${DISTDIR}/${t}"
        if [ "$MODE" = aarch64 ] && [ "$BUILD_TYPE" = Release ]; then
            # 只去调试符号,保留函数符号:盒子上崩了还能用 addr2line 定位
            aarch64-linux-gnu-strip --strip-debug -o "${dst}" "${src}" 2>/dev/null \
                || cp -f "${src}" "${dst}"
        else
            cp -f "${src}" "${dst}"
        fi
        chmod +x "${dst}"
        size=$(du -h "${dst}" | cut -f1)
        art "${dst}|${size}|${t}"
        count=$((count + 1))
    done

    # 原生构建:main / washcar_test 这类链了桩库的 target,运行时靠 $ORIGIN 找这个 .so。
    # 只编 config_test 之类不链桩库的 target 时它根本不生成,不该报"缺失"。
    if [ "$MODE" = native ]; then
        # CMake 给 SHARED 库自动加 lib 前缀:产物是 libwashcar_arch_stub.so
        f=""
        for cand in "${BUILD_DIR}/src/libwashcar_arch_stub.so" "${BUILD_DIR}/src/washcar_arch_stub.so"; do
            [ -f "$cand" ] && { f="$cand"; break; }
        done
        if [ -n "$f" ]; then
            cp -f "$f" "${DISTDIR}/"
            art "${DISTDIR}/$(basename "$f")|$(du -h "$f" | cut -f1)|桩库"
        else
            case " ${TARGETS} " in
                *" main "*|*" washcar_test "*|*" detection_test "*|*" camera_test "*|*" ai_load_test "*|*" tcp_plc_test "*)
                    warn "没找到 libwashcar_arch_stub.so(编译产物不全),这些 target 运行时可能起不来" ;;
            esac
        fi
    else
        # 盒子产物:把两个 aarch64 预编译库一起带上,现场直接整个目录覆盖过去
        for lib in \
            "${PROJECT_ROOT}/lib/rknn/3rdparty/rknpu2/Linux/aarch64/librknnrt.so" \
            "${PROJECT_ROOT}/lib/rknn/3rdparty/librga/Linux/aarch64/librga.so"; do
            if [ -f "${lib}" ]; then
                cp -f "${lib}" "${DISTDIR}/"
                art "${DISTDIR}/$(basename "${lib}")|$(du -h "${lib}" | cut -f1)|运行时库"
            fi
        done
    fi

    [ "${count}" -gt 0 ] || { err "  一个产物都没收到"; return 1; }

    # 校验清单:拷到盒子后可以 sha256sum -c 对一遍,确认没拷坏
    local sums
    sums="$( cd "${DISTDIR}" && for f in *; do [ -f "$f" ] && sha256sum "$f"; done 2>/dev/null )"
    {
        echo "# washcar build manifest"
        echo "time       : $(date '+%Y-%m-%d %H:%M:%S')"
        echo "mode       : ${MODE}"
        echo "build_type : ${BUILD_TYPE}"
        echo "targets    : ${TARGETS_DISPLAY}"
        echo "compiler   : ${CC_DESC}"
        echo "git        : $(git -C "${PROJECT_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
        echo "host       : $(uname -sr) $(uname -m)"
        echo ""
        echo "sha256 (该目录下执行 sha256sum -c MANIFEST.txt.sha256 校验):"
        printf '%s\n' "${sums}" | sed 's/^/  /'
    } >"${DISTDIR}/MANIFEST.txt" 2>/dev/null
    printf '%s\n' "${sums}" >"${DISTDIR}/MANIFEST.txt.sha256" 2>/dev/null

    if [ "$MODE" = aarch64 ]; then
        # 部署目标可以换(WC_DEPLOY_TARGET=user@host:/path),说明文字跟着变,不写死账号和 IP
        local d_user d_host d_dir
        if [ "${DEPLOY_TARGET#*@}" != "${DEPLOY_TARGET}" ]; then
            d_user="${DEPLOY_TARGET%%@*}"
            d_host="${DEPLOY_TARGET#*@}"; d_host="${d_host%%:*}"
            d_dir="${DEPLOY_TARGET#*:}"
        else
            d_user="${USER:-$(id -un)}"; d_host="<盒子IP>"; d_dir="${DEPLOY_TARGET}"
        fi
        cat >"${DISTDIR}/部署说明.txt" <<EOS
把文件拷到 RK3568 盒子（Ubuntu 20.04 / 用户 ${d_user} ${d_host}）
=================================================

1) 目标目录: ${d_dir}
   用 WinSCP / MobaXterm / U 盘 把本目录下的 main、
   librknnrt.so、librga.so 拷过去（覆盖旧的）。

   命令行方式（Windows PowerShell）:
     scp -r .\main .\librknnrt.so .\librga.so ${DEPLOY_TARGET}

2) 盒子上给执行权限:
     chmod +x ${d_dir}main

3) 重启服务:
     sudo systemctl restart washcar
     journalctl -u washcar -f        # 看日志

4) 校验文件有没有拷坏（在盒子上、这些文件所在目录执行）:
     sha256sum -c MANIFEST.txt.sha256

注意
----
* 不要覆盖盒子上的 config.json —— 那份是现场配置。
* 本目录是 Release（已 strip 调试符号，体积小）。
* 要现场 gdb/core dump 定位，用 aarch64-debug 目录的产物。
* 部署地址不对? 设 WC_DEPLOY_TARGET=用户@主机:/路径 重编一次即可。
EOS
    fi

    cp -f "${LOG}" "${DIST}/logs/build-${SUBDIR}.log" 2>/dev/null
    local n_files
    n_files=$(find "${DISTDIR}" -maxdepth 1 -type f | wc -l)
    info "DIST=${DISTDIR}"
    info "LOG=${LOG}"
    info "COMMIT=$(git -C "${PROJECT_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    note "  已回传 ${count} 个 target 共 ${n_files} 个文件 → ${DISTDIR}"
    last_count="${count}"
    return 0
}

# =============================================================================
# 运行 / 调试(native)
# =============================================================================
# 运行目录: main 和桩库用软链指过去, 配置/data/日志的基准目录就是这里(exeDir)。
prepare_run_dir() {
    local rd="${WC_RUN_DIR:-${HOME}/washcar-run-main}"
    if [ ! -x "${BUILD_DIR}/src/main" ]; then
        return 1
    fi
    mkdir -p "$rd"
    ln -sfn "${BUILD_DIR}/src/main" "$rd/main"
    # $ORIGIN 要在同目录找到桩库
    if [ -f "${BUILD_DIR}/src/libwashcar_arch_stub.so" ]; then
        ln -sfn "${BUILD_DIR}/src/libwashcar_arch_stub.so" "$rd/libwashcar_arch_stub.so"
    fi
    printf '%s' "$rd"
}

# 确保配置文件存在; 不存在就用 main 自己的模板生成一份, 并把 AI 关掉。
# 为什么要关 AI: 模板里 ai.aiEnabled=true 且带 2 个示例模型, 指向不存在的 .rknn,
# AiModelFactory 加载失败会抛 runtime_error, main 里没人接 → abort(实测)。
ensure_config() {
    local cfg="$1" rd="$2"
    if [ -s "$cfg" ]; then
        return 0
    fi
    mkdir -p "$(dirname "$cfg")"
    local tmp="${cfg}.tmp"
    rm -f "$tmp"

    ph "0/1 生成配置文件"
    # 让 main 自己吐一份完整模板(它先写文件再加载模型, 所以这里必然崩一次, 无所谓)。
    # 重定向写在子 shell 外面: 崩掉时那行 "Aborted" 是 bash 打的, 不在 ./main 的 stderr 里。
    ( cd "$rd" && WASHCAR_CONFIG="$tmp" timeout 8 ./main ) >/dev/null 2>&1 || true

    if [ -s "$tmp" ]; then
        python3 - "$tmp" <<'PY'
import json, sys
path = sys.argv[1]
cfg = json.load(open(path, encoding="utf-8"))
cfg.setdefault("ai", {})["aiEnabled"] = False
# 本地调试默认连本机 broker：直跑/[M]/手动 start.py 都用 127.0.0.1:1883
mqtt = cfg.setdefault("mqtt", {})
mqtt["brokerIp"] = "127.0.0.1"
mqtt["port"] = 1883
json.dump(cfg, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
PY
        mv -f "$tmp" "$cfg"
        note "  已生成 ${cfg}（ai.aiEnabled 已关，MQTT 默认 127.0.0.1:1883）"
    else
        printf '{ "ai": { "aiEnabled": false } }\n' >"$cfg"
        warn "  模板生成失败, 写了一份最小配置(其余字段走内置默认)"
    fi
    return 0
}

# main 直接跑:不掺 mock, 就是你自己起 main 看日志。
# 坑:模板配置默认 ai.aiEnabled=true + 带 2 个示例模型, 指向不存在的 .rknn,
#     AiModelFactory 加载失败直接抛 runtime_error 没人接 → abort。所以首次生成的
#     配置里要把 AI 关掉, 否则一跑就崩(实测)。
run_main_direct() {
    local rd
    rd="$(prepare_run_dir)" || die 1 "没有可执行产物: ${BUILD_DIR}/src/main(先编译)"
    local secs="${WC_RUN_SECONDS:-0}"   # 0 = 一直跑，Ctrl+C 停

    ensure_config "${CONFIG_PATH}" "$rd"

    local mqtt_line
    mqtt_line=$(python3 - "$CONFIG_PATH" <<'PY'
import json, sys
try:
    cfg = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('?|?|?|1')
    raise SystemExit
mqtt = cfg.get('mqtt', {}) or {}
box = cfg.get('box', {}) or {}
ip = str(mqtt.get('brokerIp', '?'))
port = str(mqtt.get('port', '?'))
warn = '1' if (':' in ip or ip in ('0.0.0.0', '')) else '0'
print('%s|%s|%s|%s' % (ip, port, box.get('boxNo', '?'), warn))
PY
)
    local mqtt_ip mqtt_port mqtt_box mqtt_warn
    IFS='|' read -r mqtt_ip mqtt_port mqtt_box mqtt_warn <<<"$mqtt_line"
    ph "1/1 直接运行 main"
    note "  ⚠ 直跑模式：不启虚拟 PLC / broker / 看板，也不会自动下发洗车命令"
    note "    MQTT: brokerIp=${mqtt_ip} port=${mqtt_port} boxNo=${mqtt_box}"
    if [ "${mqtt_ip}" = "127.0.0.1" ] || [ "${mqtt_ip}" = "localhost" ]; then
        note "    → 连本机 broker：要先把模拟器起来（python3 start.py，或 [M]）才有 1883"
    fi
    if [ "${mqtt_warn:-0}" = "1" ]; then
        note "    ⚠ brokerIp 写成 \"${mqtt_ip}\" 有问题：只填 IP，端口写 port；0.0.0.0 不能作为目的地"
    fi
    note "    想看全链路（自动复位+启动、网页看板）请用 [M] 全链路模拟"
    note "  运行目录: ${rd}   (data / 日志在这里, 不污染构建目录)"
    note "  配置文件: ${CONFIG_PATH}   ← 直接用 Windows 编辑器改这个"
    if [ "$secs" -gt 0 ]; then
        note "  跑 ${secs}s 后自动停(要看更久: -RunSeconds 0, 或直接 Ctrl+C)"
    else
        note "  一直跑, Ctrl+C 停"
    fi
    note "  本地没起模拟器时, 日志里会持续报 PLC / MQTT 连接失败 —— 这是正常的"

    # 配置改坏了要给一句人话, 而不是让 main 报一堆找不到字段
    local jerr
    if ! jerr=$(python3 -c 'import json,sys; json.load(open(sys.argv[1],encoding="utf-8"))' "$CONFIG_PATH" 2>&1); then
        err "  配置文件 JSON 解析失败, 先修好再跑: ${CONFIG_PATH}"
        printf '%s\n' "$jerr" | tail -2 | while IFS= read -r l; do printf '@@ERRCTX@@ %s\n' "$l"; done
        printf '@@END@@ fail %s 0\n' "$(elapsed)"
        return 1
    fi

    local -a tmo=()
    [ "$secs" -gt 0 ] && tmo=(timeout "$secs")
    ( cd "$rd" && WASHCAR_CONFIG="${CONFIG_PATH}" ${tmo[@]+"${tmo[@]}"} ./main ) 2>&1 \
        | while IFS= read -r line; do printf '@@TAIL@@ %s\n' "${line%$'\r'}"; done
    local rc=${PIPESTATUS[0]}
    case "$rc" in
        0)        note "  main 正常退出(退出码 0)" ;;
        124|137)  note "  到点停止(进程是被 timeout 收掉的, 不是崩了)" ;;
        *)        err "  main 异常退出: 退出码 ${rc}(看上面的日志)" ;;
    esac
    printf '@@END@@ %s %s 0\n' "$([ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && echo ok || echo fail)" "$(elapsed)"
    return 0
}

# main 单跑没意义:它要连 FX PLC 串口 + MQTT broker,直接起只会报错退出。
# 走 tools/plc-mock 的全链路闭环:梯形图虚拟 PLC(PTY 冒充串口) + 迷你 broker + main。
run_main_mock() {
    # 只依赖这一个脚本 + 两个环境变量(BUILD_DIR / TIME_SCALE),输出原样转发不解析。
    # 想换脚本:WC_MOCK_SCRIPT=/path/to/xxx.sh;tools/plc-mock 怎么改都不会影响编译那条路。
    local mock="${WC_MOCK_SCRIPT:-${PROJECT_ROOT}/tools/plc-mock/run_local_native.sh}"
    [ -f "$mock" ] || die 1 "找不到 ${mock}（tools/ 没同步到 ~/washcar-wsl,必须从仓库路径跑）"
    local ts="${WC_TIME_SCALE:-25}"
    ph "1/1 全链路闭环 main（虚拟 PLC + 迷你 broker）"
    note "  main 单跑没意义:它要连 FX PLC 串口和 MQTT broker,直接起只会报错退出。"
    note "  这里起【梯形图虚拟 PLC + 自建 MQTT broker + 网页看板】再起 main,跑一轮完整洗车:"
    note "  采集 → 状态机 → 故障判定 → MQTT 上行 → 【自动注入云端复位+启动】 → 写 PLC 回执"
    note "  时间倍速 ${ts}×(约 $((430 / ts + 30))s 跑完);照实跑用 WC_TIME_SCALE=1(约 7 分钟)"
    note "  前 25 秒左右没有输出是在等 mock 起来,不是卡死"
    ( cd "${PROJECT_ROOT}" \
      && BUILD_DIR="${BUILD_DIR}/src" TIME_SCALE="${ts}" \
         timeout "${WC_RUN_TIMEOUT:-300}" bash "$mock" ) 2>&1 \
      | while IFS= read -r line; do printf '@@TAIL@@ %s\n' "${line%$'\r'}"; done
    local rc=${PIPESTATUS[0]}
    if [ "$rc" -eq 0 ]; then
        note "  闭环跑完（详细日志: ~/plcmock-run-native/）"
    else
        err "  退出码 ${rc}（日志: ~/plcmock-run-native/ladder_main.log）"
    fi
    printf '@@END@@ %s %s 0\n' "$([ "$rc" -eq 0 ] && echo ok || echo fail)" "$(elapsed)"
    return "$rc"
}

run_target() {
    local t="${TARGET_NAME}"
    [ -n "$t" ] || die 3 "缺少 --target-name"
    local bin="${BUILD_DIR}/src/${t}"
    [ -x "$bin" ] || die 1 "没有可执行产物: ${bin}(先编译)"

    if [ "$t" = "main" ] && [ "$MODE" = native ]; then
        # WC_RUN_MODE: direct(默认, 直接起 main) / mock(起虚拟 PLC + broker 跑全链路)
        case "${WC_RUN_MODE:-direct}" in
            mock) run_main_mock ;;
            *)    run_main_direct ;;
        esac
        return $?
    fi

    local run_dir="${BUILD_DIR}/run"
    mkdir -p "$run_dir"
    ph "1/1 运行 ${t}"
    note "  工作目录: ${run_dir}"
    ( cd "${run_dir}" \
      && LD_LIBRARY_PATH="${BUILD_DIR}/src:${LD_LIBRARY_PATH:-}" \
         timeout "${RUN_TIMEOUT}" "$bin" ) 2>&1 \
      | while IFS= read -r line; do printf '@@TAIL@@ %s\n' "${line%$'\r'}"; done
    local rc=${PIPESTATUS[0]}
    if [ "$rc" -eq 0 ]; then
        note "  退出码 0(通过)"
    elif [ "$rc" -eq 124 ]; then
        err "  超时 ${RUN_TIMEOUT}s"
    else
        err "  退出码 ${rc}"
    fi
    printf '@@END@@ %s %s 0\n' "$([ "$rc" -eq 0 ] && echo ok || echo fail)" "$(elapsed)"
    return "$rc"
}

gdb_target() {
    local t="${TARGET_NAME}"
    [ -n "$t" ] || die 3 "缺少 --target-name"
    local bin="${BUILD_DIR}/src/${t}"
    [ -x "$bin" ] || die 1 "没有可执行产物: ${bin}(先编译)"
    command -v gdb >/dev/null 2>&1 || die 2 "WSL 里没装 gdb: sudo apt install -y gdb"

    # 快速档用 -g0 编的:符号表还在(函数名有),但没有 .debug_info,所以栈里没有行号。
    # 这个不说明清楚,用户会以为 gdb 坏了。
    if ! readelf -S "$bin" 2>/dev/null | grep -q '\.debug_info'; then
        warn "这个产物没有调试信息(快速档 -O0 -g0),栈里只有函数名、不会有源码行号"
        hint "  要行号:用「调试」档重编 —— 菜单里选它,或加 -BuildType Debug"
        hint "  例: .\\一键编译.cmd -Mode native -Targets ${t} -BuildType Debug -Yes"
    fi

    local run_dir="${BUILD_DIR}/run"
    mkdir -p "$run_dir"
    ph "1/1 gdb 跑一遍 ${t}（崩了自动打完整栈）"
    note "  交互式调:   WSL 里执行 cd ${BUILD_DIR}/src && gdb ./${t}"
    ( cd "${run_dir}" \
      && LD_LIBRARY_PATH="${BUILD_DIR}/src:${LD_LIBRARY_PATH:-}" \
         timeout 600 gdb -q -batch \
            -ex 'set pagination off' -ex 'set print pretty on' \
            -ex run -ex 'bt full' -ex 'info registers' -ex quit --args "$bin" ) 2>&1 \
      | while IFS= read -r line; do printf '@@TAIL@@ %s\n' "${line%$'\r'}"; done
    local rc=${PIPESTATUS[0]}
    printf '@@END@@ %s %s 0\n' "$([ "$rc" -eq 0 ] && echo ok || echo fail)" "$(elapsed)"
    return "$rc"
}

# =============================================================================
# 主流程
# =============================================================================
last_count=0

case "$ACTION" in
    check)
        check_env || { printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit 2; }
        note "构建目录: ${BUILD_DIR}"
        note "回传目录: ${DISTDIR}"
        printf '@@END@@ ok %s 0\n' "$(elapsed)"
        exit 0
        ;;
    clean)
        rm -rf "${BUILD_DIR}"
        note "已删除 ${BUILD_DIR}"
        printf '@@END@@ ok %s 0\n' "$(elapsed)"
        exit 0
        ;;
    list)
        # 给 build-menu.ps1 用:菜单里的目标/说明全部来自这里,不在 PS 里另写一份
        for _t in ${DISCOVERED}; do
            printf '@@TARGET@@ %s|%s\n' "${_t}" "$(target_desc "${_t}")"
        done
        printf '@@END@@ ok %s 0\n' "$(elapsed)"
        exit 0
        ;;
    config)
        # 只准备配置文件, 不跑 main: 给"改配置 → 重跑"这个循环用。
        _rd="$(prepare_run_dir)" || die 1 "没有可执行产物: ${BUILD_DIR}/src/main(先编译)"
        ensure_config "${CONFIG_PATH}" "${_rd}"
        info "CONFIG=${CONFIG_PATH}"
        note "  配置文件: ${CONFIG_PATH}"
        note "  改完回到菜单按 [R] 就是用它跑;数据/日志在 ${_rd}"
        printf '@@END@@ ok %s 0\n' "$(elapsed)"
        exit 0
        ;;
    run)  run_target; exit $? ;;
    gdb)  gdb_target; exit $? ;;
esac

# ---- build ----
mkdir -p "${BUILD_DIR}"

# 同一份源码副本不能被两个 build 同时 rsync --delete,加锁挡一下
# 锁按源码副本区分: 同一份副本不允许并发(rsync --delete 会互踩),
# 不同仓库/不同人互不阻塞,也不会被别处遗留的同名锁挡住。
LOCK="/tmp/washcar-build-$(printf '%s' "${WORK}" | md5sum | cut -c1-8).lock"
if command -v flock >/dev/null 2>&1; then
    exec 9>"${LOCK}"
    if ! flock -n 9; then
        die 1 "另一个编译正在跑(锁 ${LOCK}),等它结束再来"
    fi
fi

note "构建方式: $([ "$MODE" = native ] && echo '原生 x86_64' || echo 'RK3568 aarch64')  ·  ${BUILD_TYPE}  ·  -j ${JOBS}"
note "源码副本: ${WORK}"
note "构建目录: ${BUILD_DIR}"
# 先报出去:失败时 PowerShell 也要能显示日志在哪(失败路径不会走到 collect_artifacts)
info "LOG=${LOG}"
info "DIST=${DISTDIR}"

check_env || { printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit 2; }

if [ "$DO_SYNC" -eq 1 ]; then
    do_sync || { printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit 1; }
else
    ph "2/5 同步源码(--no-sync,跳过)"
fi

do_configure || { printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit 1; }

# 菜单给的是"CMakeLists 里写了的全部 target",这里按当前构建模式收敛成实际能编的
prune_missing_targets || { printf '@@END@@ fail %s 0\n' "$(elapsed)"; exit 1; }

if ! do_build; then
    report_failure
    printf '@@END@@ fail %s 0\n' "$(elapsed)"
    exit 1
fi

if ! collect_artifacts; then
    printf '@@END@@ fail %s 0\n' "$(elapsed)"
    exit 1
fi

printf '@@END@@ ok %s %s\n' "$(elapsed)" "${last_count}"
exit 0
