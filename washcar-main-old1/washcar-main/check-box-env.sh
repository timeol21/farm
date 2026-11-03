#!/bin/bash
# =============================================================================
# 盒子端环境自检（RK3568 / aarch64 / Ubuntu 20.04）
#
# 只读检查，不改任何东西，不需要 root。
# 对应 washcar 的依赖要求（来源：README.md / wsl-cross/* / src/CMakeLists.txt）：
#   运行时： glibc >= 2.31, libstdc++ GLIBCXX_3.4.28,
#            libssl.so.1.1 / libcrypto.so.1.1 (OpenSSL 1.1.1f),
#            libavcodec.so.58 / libavformat.so.58 / libavutil.so.56 / libswscale.so.5
#            (FFmpeg 4.2), flock(util-linux), systemd, ztl 用户
#   随仓库： librknnrt.so(2.3.2) / librga.so / 3 个 .rknn 模型 —— 不用 apt 装
#   编译时(本机编译才需要)： g++(9.x) / cmake>=3.10 / make /
#            nlohmann-json3-dev / libssl-dev / libav{codec,format,util}-dev / libswscale-dev
#
# 用法：bash check-box-env.sh          （脚本放哪都行，自动向上找仓库根）
#       SRC_DIR=/home/ztl/program/washcar-main/washcar-main bash check-box-env.sh
# 退出码：0 全部通过(允许 WARN)，1 存在 FAIL
# =============================================================================
set -u
 
FAILED=0
WARNED=0
ok()   { echo "  [OK]    $*"; }
bad()  { echo "  [FAIL]  $*"; FAILED=1; }
warn() { echo "  [WARN]  $*"; WARNED=1; }
info() { echo "  [INFO]  $*"; }
 
# 版本比较：ver_ge A B → A >= B 返回 0
ver_ge() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}
 
# 从某目录一路向上找 washcar 仓库根（含 lib/rknn/3rdparty 的那层）。
# 处理 /home/ztl/program/washcar-main/washcar-main 这种外层再包一层的上传结构。
find_repo_root() {
    local d="$1"
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        if [ -d "$d/lib/rknn/3rdparty" ]; then
            echo "$d"
            return 0
        fi
        d="$(dirname "$d")"
    done
    return 1
}
 
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -z "${SRC_DIR:-}" ]; then
    SRC_DIR="$(find_repo_root "$SCRIPT_DIR" || find_repo_root "$PWD" || true)"
    [ -z "$SRC_DIR" ] && SRC_DIR="$PWD"
fi
RUN_DIR="${RUN_DIR:-/home/ztl/washcar/deployed}"
 
echo "============================================="
echo " washcar 盒子端环境自检"
echo " 源码目录: $SRC_DIR"
echo " 运行目录: $RUN_DIR"
echo "============================================="
 
# ---------- 1. 系统基础 ----------
echo
echo "[1/6] 系统基础"
ARCH="$(uname -m)"
if [ "$ARCH" = "aarch64" ]; then ok "CPU 架构 aarch64"; else bad "CPU 架构是 $ARCH，期望 aarch64（本程序只编了 arm64）"; fi
 
if [ -r /etc/os-release ]; then
    . /etc/os-release
    if [ "${ID:-}" = "ubuntu" ]; then
        if [ "${VERSION_ID:-}" = "20.04" ]; then
            ok "系统 Ubuntu 20.04 (focal, glibc 2.31) —— 与交叉编译基线一致"
        else
            bad "系统是 Ubuntu ${VERSION_ID:-?}，项目基线是 20.04：22.04 没有 libssl1.1(只有 OpenSSL 3)且 libavformat 是 .59，交叉编译产物会报 libssl.so.1.1 / libavformat.so.58 not found。请换 20.04 镜像或联系维护者重编"
        fi
    else
        warn "系统不是 Ubuntu（${NAME:-未知}），以下库版本判断以 soname 为准"
    fi
else
    warn "读不到 /etc/os-release"
fi
 
INIT1="$(ps -p 1 -o comm= 2>/dev/null)"
if [ "$INIT1" = "systemd" ]; then
    ok "PID 1 是 systemd（可装 washcar.service 开机自启）"
else
    warn "PID 1 不是 systemd，只能手工跑 ./main，装不了 deploy/washcar.service"
fi
 
if id -u ztl >/dev/null 2>&1; then ok "ztl 用户存在"; else warn "ztl 用户不存在（systemd unit 和部署脚本都按 ztl 写的）"; fi
 
if command -v flock >/dev/null 2>&1; then
    ok "flock 可用（util-linux，单实例锁要用）"
else
    bad "缺 flock（util-linux）：sudo apt install util-linux"
fi
 
# ---------- 2. 运行时系统库（对应 ldd 需求） ----------
echo
echo "[2/6] 运行时系统库（soname 必须完全一致）"
 
# getconf 给的是干净的 "glibc 2.31"；ldd 那行含 "2.31-0ubuntu9.18" 会匹配出多个版本号，只作兜底
GLIBC_VER="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
if [ -z "$GLIBC_VER" ]; then
    GLIBC_VER="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
fi
if [ -n "$GLIBC_VER" ]; then
    if ver_ge "$GLIBC_VER" 2.31; then
        ok "glibc $GLIBC_VER (>= 2.31)"
    else
        bad "glibc $GLIBC_VER < 2.31，产物要求 >= 2.31（Ubuntu 20.04 基线）"
    fi
else
    bad "无法获取 glibc 版本"
fi
 
STDCXX_PATH="$(ldconfig -p 2>/dev/null | awk '/libstdc\+\+\.so\.6 /{print $NF; exit}')"
STDCXX_PATH="${STDCXX_PATH:-/usr/lib/aarch64-linux-gnu/libstdc++.so.6}"
if [ -r "$STDCXX_PATH" ]; then
    if grep -aq GLIBCXX_3.4.28 "$STDCXX_PATH"; then
        ok "libstdc++ 提供 GLIBCXX_3.4.28 (gcc-9 基线, $STDCXX_PATH)"
    else
        bad "$STDCXX_PATH 缺 GLIBCXX_3.4.28 符号（gcc-9 编译产物跑不起来）"
    fi
else
    bad "找不到 libstdc++.so.6"
fi
 
check_soname() {  # $1=soname 正则 $2=说明
    if ldconfig -p 2>/dev/null | grep -qE "$1"; then
        ok "$2"
    else
        bad "$2 —— 缺失"
    fi
}
check_soname 'libssl\.so\.1\.1 '     "libssl.so.1.1    (OpenSSL 1.1.1)"
check_soname 'libcrypto\.so\.1\.1 '  "libcrypto.so.1.1 (OpenSSL 1.1.1)"
check_soname 'libavcodec\.so\.58 '   "libavcodec.so.58 (FFmpeg 4.2)"
check_soname 'libavformat\.so\.58 '  "libavformat.so.58 (FFmpeg 4.2)"
check_soname 'libavutil\.so\.56 '    "libavutil.so.56  (FFmpeg 4.2)"
check_soname 'libswscale\.so\.5 '    "libswscale.so.5  (FFmpeg 4.2)"
if ldconfig -p 2>/dev/null | grep -qE 'libswresample\.so\.3 '; then
    ok "libswresample.so.3 (libavcodec 的间接依赖)"
else
    warn "libswresample.so.3 不在 ldconfig 缓存里（装 libavcodec58 会自动带上）"
fi
 
# ---------- 3. NPU（RKNN）与仓库自带库 ----------
echo
echo "[3/6] NPU / RKNN 与仓库自带库"
if [ -d "$SRC_DIR/lib/rknn/3rdparty" ]; then
    ok "仓库根识别: $SRC_DIR"
else
    bad "$SRC_DIR 不是 washcar 仓库根（没找到 lib/rknn/3rdparty）。实际路径用 SRC_DIR=... 传入"
fi
 
RKN_RT="$SRC_DIR/lib/rknn/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
RKN_RGA="$SRC_DIR/lib/rknn/3rdparty/librga/Linux/aarch64/librga.so"
if [ -f "$RKN_RT" ]; then
    RT_VER="$(grep -aoE 'librknnrt version: [0-9.]+' "$RKN_RT" | head -1 | cut -d' ' -f3)"
    ok "仓库自带 librknnrt.so ${RT_VER:-?}（运行时放二进制同级目录，RUNPATH=\$ORIGIN 找到）"
else
    bad "仓库缺 $RKN_RT —— 上传的源码不完整（lib/rknn/3rdparty 没传全）"
fi
if [ -f "$RKN_RGA" ]; then
    ok "仓库自带 librga.so（CMake 已加 DISABLE_RGA 走 CPU，仅链接需要）"
else
    bad "仓库缺 $RKN_RGA"
fi
for m in yolov8.rknn ppocrv4_det.rknn lprnet.rknn; do
    if [ -f "$SRC_DIR/lib/rknn/models/$m" ]; then
        ok "模型存在: lib/rknn/models/$m"
    else
        warn "缺模型 lib/rknn/models/$m（AI 检测起不来，业务不受影响）"
    fi
done
 
NPU_SYS="$(ls /sys/bus/platform/devices/ 2>/dev/null | grep -i npu | head -1)"
if [ -n "$NPU_SYS" ]; then
    ok "内核 NPU 设备节点存在: $NPU_SYS"
else
    warn "/sys/bus/platform/devices 下没找到 npu 设备 —— AI 检测跑不了（业务日志/PLC 不受影响）"
fi
DMESG_OUT="$(dmesg 2>/dev/null | grep -i rknpu | head -1)"
[ -n "$DMESG_OUT" ] && info "dmesg rknpu: $DMESG_OUT"
 
# ---------- 4. 编译环境（只在本机编译时需要；走 WSL 交叉编译可忽略 FAIL） ----------
echo
echo "[4/6] 编译环境（本机编译用；纯部署二进制可忽略本节 FAIL）"
if command -v g++ >/dev/null 2>&1; then
    GPP_VER="$(g++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
    GPP_MAJOR="${GPP_VER%%.*}"
    if [ "$GPP_MAJOR" = "9" ]; then
        ok "g++ $GPP_VER（与项目 gcc-9 基线一致）"
    elif [ "$GPP_MAJOR" -lt 9 ] 2>/dev/null; then
        bad "g++ $GPP_VER 太老（C++17/filesystem 需要 >= 9）"
    else
        warn "g++ $GPP_VER 不是 9.x：能编，但可能要求比系统 libstdc++ 更新的 GLIBCXX 符号；建议用 g++-9（20.04 默认）"
    fi
else
    bad "没有 g++（sudo apt install build-essential）"
fi
 
if command -v cmake >/dev/null 2>&1; then
    CMAKE_VER="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
    if ver_ge "$CMAKE_VER" 3.10; then
        ok "cmake $CMAKE_VER (>= 3.10)"
    else
        bad "cmake $CMAKE_VER < 3.10"
    fi
else
    bad "没有 cmake（sudo apt install cmake）"
fi
if command -v make >/dev/null 2>&1; then ok "make 可用"; else bad "缺 make（sudo apt install build-essential）"; fi
 
check_dev() {  # $1=文件 $2=包名 $3=说明
    if [ -e "$1" ]; then ok "$3"; else bad "$3 —— 缺 $2"; fi
}
check_dev /usr/include/nlohmann/json.hpp        nlohmann-json3-dev  "nlohmann/json.hpp 头文件"
check_dev /usr/lib/aarch64-linux-gnu/libssl.so     libssl-dev      "libssl 链接符号（OpenSSL 开发包）"
check_dev /usr/lib/aarch64-linux-gnu/libavcodec.so libavcodec-dev  "libavcodec 链接符号"
check_dev /usr/lib/aarch64-linux-gnu/libavformat.so libavformat-dev "libavformat 链接符号"
check_dev /usr/lib/aarch64-linux-gnu/libavutil.so  libavutil-dev   "libavutil 链接符号"
check_dev /usr/lib/aarch64-linux-gnu/libswscale.so libswscale-dev  "libswscale 链接符号"
# sqlite 是源码内置(sqlite3.c)，不需要系统开发包，这里只是提示
if command -v sqlite3 >/dev/null 2>&1; then
    ok "sqlite3 命令行可用（install.sh 验证 outbox 用）"
else
    warn "没有 sqlite3 CLI（仅影响人工查 command.db，不影响程序运行；装: sudo apt install sqlite3）"
fi
 
# ---------- 5. 运行目录（部署过才有内容） ----------
echo
echo "[5/6] 运行目录 $RUN_DIR"
if [ -x "$RUN_DIR/main" ]; then
    ok "主程序存在: $RUN_DIR/main"
    MISS_SO="$(cd "$RUN_DIR" && ldd ./main 2>&1 | grep 'not found' | awk '{print $1}' | sort -u | tr '\n' ' ')"
    if [ -z "$MISS_SO" ]; then
        ok "ldd ./main 全部依赖可解析"
    else
        bad "ldd 报缺失: $MISS_SO"
    fi
    for m in librknnrt.so librga.so; do
        if [ -f "$RUN_DIR/$m" ]; then ok "$RUN_DIR/$m 已就位"; else bad "缺 $RUN_DIR/$m（RUNPATH=\$ORIGIN，必须和 main 同目录）"; fi
    done
    for m in yolov8.rknn ppocrv4_det.rknn lprnet.rknn; do
        if [ -f "$RUN_DIR/lib/rknn/models/$m" ]; then ok "模型 $m 已就位"; else warn "缺 $RUN_DIR/lib/rknn/models/$m（程序按 exe 同级的 lib/rknn/models 找模型）"; fi
    done
else
    info "还没部署（$RUN_DIR/main 不存在）—— 接下来跑 box-deploy/build-on-box.sh"
fi
 
# ---------- 6. 外部资源（信息级，不判 FAIL） ----------
echo
echo "[6/6] 外部资源（仅提示）"
if ls /dev/ttyUSB* >/dev/null 2>&1; then
    info "串口设备: $(ls /dev/ttyUSB* | tr '\n' ' ')"
else
    info "没有 /dev/ttyUSB*（默认配置用虚拟 PLC /tmp/vplc-pty，接真 PLC 时改 config.json 的 plcSerial.port）"
fi
[ -e /tmp/vplc-pty ] && info "虚拟 PLC 已在跑: /tmp/vplc-pty"
if timeout 3 bash -c 'echo > /dev/tcp/116.204.32.197/1883' 2>/dev/null; then
    info "MQTT broker 116.204.32.197:1883 可达（默认配置）"
else
    warn "MQTT broker 116.204.32.197:1883 不可达（3s 超时）——外网/防火墙问题，程序会自动重连"
fi
DISK_LINE="$(df -h /home 2>/dev/null | sed -n 2p)"
[ -n "$DISK_LINE" ] && info "磁盘(/home): $DISK_LINE"
MEM_LINE="$(free -h 2>/dev/null | sed -n 2p)"
[ -n "$MEM_LINE" ] && info "内存: $MEM_LINE （本机编译建议留 1G+，不够就 JOBS=1）"
 
# ---------- 汇总 ----------
echo
echo "============================================="
if [ "$FAILED" = 0 ]; then
    if [ "$WARNED" = 1 ]; then
        echo " 结果: PASS（含 WARN，看上面 [WARN] 行决定要不要处理）"
    else
        echo " 结果: PASS"
    fi
    exit 0
else
    echo " 结果: FAIL —— 按上面 [FAIL] 逐条修，缺包跑: bash box-deploy/setup-box-env.sh"
    exit 1
fi