#!/bin/bash
# =============================================================================
# 盒子端环境自检（RK3568 / aarch64 / Ubuntu 20.04）
#
# 只读检查，不改任何东西，不需要 root。
#
# washcar-test 的依赖比 washcar-val 少得多（不含摄像头/AI，所以没有
# rknn / rga / ffmpeg / opencv 那一堆）：
#   运行时: glibc >= 2.31、libstdc++ 提供 GLIBCXX_3.4.28、
#           libssl.so.1.1 + libcrypto.so.1.1（libmosquitto 依赖 OpenSSL 1.1）、
#           libmosquitto.so.1（随仓库带上，不装系统包）
#   编译时(盒子本机编译才需要): g++ 9.x / cmake>=3.10 / make
#           mosquitto 和 json 头文件走仓库内置 third_party/，不需要 apt 装
#
# 用法: bash check-box-env.sh
#       SRC_DIR=/home/ztl/program/washcar-test bash check-box-env.sh
# 退出码: 0 全部通过(允许 WARN)，1 存在 FAIL
# =============================================================================
set -u

FAILED=0
WARNED=0
ok()   { echo "  [OK]    $*"; }
bad()  { echo "  [FAIL]  $*"; FAILED=1; }
warn() { echo "  [WARN]  $*"; WARNED=1; }
info() { echo "  [INFO]  $*"; }

ver_ge() {  # A >= B
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# 向上找项目根: 第一个含 src/ 目录的层
find_repo_root() {
    local d="$1"
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        if [ -d "$d/src" ]; then
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
RUN_DIR="${RUN_DIR:-$SRC_DIR/deployed}"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build-box}"

MOSQ_HDR="$SRC_DIR/third_party/mosquitto/include/mosquitto.h"
JSON_HDR="$SRC_DIR/third_party/nlohmann/json.hpp"

# 内置 libmosquitto 的实际路径：不写死文件名，因为 Windows 上传后 .so / .so.1
# 这两个软链会丢，只剩 libmosquitto.so.1.6.9。按目录 glob，短名字优先。
find_vendor_mosq() {
    local d f
    for d in "$SRC_DIR/third_party/mosquitto/lib/aarch64" "$SRC_DIR/third_party/mosquitto/lib"; do
        [ -d "$d" ] || continue
        for f in "$d/libmosquitto.so" "$d/libmosquitto.so.1" "$d"/libmosquitto.so.*; do
            [ -s "$f" ] && { echo "$f"; return 0; }   # -s: 跳过 0 字节空壳
        done
    done
    return 1
}
MOSQ_USER="$(find_vendor_mosq || true)"

echo "============================================="
echo " washcar-test 盒子端环境自检"
echo " 源码目录: $SRC_DIR"
echo " 构建目录: $BUILD_DIR"
echo " 运行目录: $RUN_DIR"
echo "============================================="

# ---------- 1. 系统基础 ----------
echo
echo "[1/6] 系统基础"
ARCH="$(uname -m)"
if [ "$ARCH" = "aarch64" ]; then
    ok "CPU 架构 aarch64"
else
    bad "CPU 架构是 $ARCH，期望 aarch64（RK3568）"
fi

if [ -r /etc/os-release ]; then
    . /etc/os-release
    if [ "${ID:-}" = "ubuntu" ]; then
        case "${VERSION_ID:-}" in
            20.04) ok "系统 Ubuntu 20.04 (focal, glibc 2.31) —— 与交叉编译基线一致" ;;
            22.04) warn "系统 Ubuntu 22.04：glibc 2.35 能跑 20.04 产物；但若用「盒子本机编译」，请确认有 g++" ;;
            *)     warn "系统 Ubuntu ${VERSION_ID:-?}（基线是 20.04）" ;;
        esac
    else
        warn "系统不是 Ubuntu（${NAME:-未知}），以下判断以 soname 为准"
    fi
else
    warn "读不到 /etc/os-release"
fi

INIT1="$(ps -p 1 -o comm= 2>/dev/null)"
if [ "$INIT1" = "systemd" ]; then
    ok "PID 1 是 systemd（可做开机自启）"
else
    warn "PID 1 不是 systemd（$INIT1），只能手工跑 ./main"
fi

# ---------- 2. 运行时系统库 ----------
echo
echo "[2/6] 运行时系统库（soname 必须完全一致）"
GLIBC_VER="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
[ -z "$GLIBC_VER" ] && GLIBC_VER="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
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
        ok "libstdc++ 提供 GLIBCXX_3.4.28（gcc-9 基线）"
    else
        bad "$STDCXX_PATH 缺 GLIBCXX_3.4.28 符号"
    fi
else
    bad "找不到 libstdc++.so.6"
fi

# 关键: 仓库内置的 libmosquitto 依赖 OpenSSL 1.1（focal 的 soname 是 .so.1.1）
# 只有 20.04 基线才把它当硬性 FAIL；22.04+ 自带 OpenSSL 3，没有 1.1 是正常的
# （那种系统上只能跑在本机编的产物，不能用 20.04 的交叉编译产物）。
check_soname() {
    if ldconfig -p 2>/dev/null | grep -qE "$1"; then ok "$2"; else bad "$2 —— 缺失"; fi
}
check_openssl11() {
    local pat="$1" desc="$2"
    if ldconfig -p 2>/dev/null | grep -qE "$pat"; then
        ok "$desc"
    elif [ "${VERSION_ID:-}" = "20.04" ]; then
        bad "$desc —— 缺失（20.04 必须带，libmosquitto 依赖它）"
        info "  装法: sudo apt-get install -y libssl1.1"
        info "  离线盒子: 从同级盒子拷 /usr/lib/aarch64-linux-gnu/lib{ssl,crypto}.so.1.1"
    else
        warn "$desc 不在 ldconfig 缓存（非 20.04 正常现象；若在 20.04 盒子上跑必须补齐）"
    fi
}
check_openssl11 'libssl\.so\.1\.1 '    "libssl.so.1.1    (libmosquitto 依赖, OpenSSL 1.1)"
check_openssl11 'libcrypto\.so\.1\.1 ' "libcrypto.so.1.1 (libmosquitto 依赖, OpenSSL 1.1)"

# ---------- 3. 仓库内置的第三方依赖 ----------
echo
echo "[3/6] 仓库内置依赖（third_party/，盒子无需 apt 安装）"
if [ -n "$MOSQ_USER" ] && [ -f "$MOSQ_USER" ]; then
    ok "内置 libmosquitto 就位（${MOSQ_USER#$SRC_DIR/}）"
else
    bad "third_party/mosquitto/lib/aarch64 下没有 libmosquitto.so* —— 上传的源码不完整"
    info "  在 Windows 上执行: rsync -av third_party/ <盒子>:<项目路径>/third_party/"
fi
if [ -f "$MOSQ_HDR" ]; then
    ok "mosquitto.h 就位（本机编译需要）"
else
    bad "缺 $MOSQ_HDR"
fi
if [ -f "$JSON_HDR" ]; then
    ok "nlohmann/json.hpp 就位（本机编译需要）"
else
    bad "缺 $JSON_HDR"
fi

# ---------- 4. 编译环境（盒子本机编译用） ----------
echo
echo "[4/6] 编译环境（只影响「盒子本机编译」；纯部署二进制可忽略本节 FAIL）"
if command -v g++ >/dev/null 2>&1; then
    GPP_VER="$(g++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
    GPP_MAJOR="${GPP_VER%%.*}"
    if [ "$GPP_MAJOR" -ge 9 ] 2>/dev/null; then
        ok "g++ $GPP_VER（>= 9，满足 C++17）"
    else
        bad "g++ $GPP_VER 太老（C++17 需要 >= 9）"
    fi
else
    bad "没有 g++（sudo apt-get install -y build-essential）"
fi

if command -v cmake >/dev/null 2>&1; then
    CMAKE_VER="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
    if ver_ge "$CMAKE_VER" 3.10; then
        ok "cmake $CMAKE_VER (>= 3.10)"
    else
        bad "cmake $CMAKE_VER < 3.10"
    fi
else
    bad "没有 cmake（sudo apt-get install -y cmake）"
fi

if command -v make >/dev/null 2>&1; then ok "make 可用"; else bad "缺 make"; fi
if command -v strip >/dev/null 2>&1; then ok "strip 可用（部署版会瘦身）"; else warn "缺 strip（binutils），部署版体积偏大"; fi
if command -v readelf >/dev/null 2>&1; then ok "readelf 可用（校验 RUNPATH）"; else warn "缺 readelf（binutils）"; fi

# ---------- 5. 运行目录 ----------
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
    if [ -f "$RUN_DIR/libmosquitto.so.1" ]; then
        ok "libmosquitto.so.1 与 main 同目录（RUNPATH=\$ORIGIN 靠它加载）"
    else
        bad "缺 $RUN_DIR/libmosquitto.so.1"
    fi
    if [ -f "$RUN_DIR/config.json" ]; then
        ok "config.json 已就位"
    else
        info "还没有 config.json —— 首次 ./main 会自动生成模板"
    fi
else
    info "还没编译（$RUN_DIR/main 不存在）—— 跑: bash build-on-box.sh"
fi

# ---------- 6. 外部资源（只提示） ----------
echo
echo "[6/6] 外部资源（仅提示）"
if ls /dev/ttyUSB* >/dev/null 2>&1; then
    info "串口设备: $(ls /dev/ttyUSB* | tr '\n' ' ')"
else
    info "没有 /dev/ttyUSB*（接真 PLC 时改 config.json 的 plcSerial.port，海天常见 /dev/ttyUSB10）"
fi
[ -e /tmp/vplc-pty ] && info "虚拟 PLC 已在跑: /tmp/vplc-pty"

MQTT_HOST="$(grep -A5 '"mqtt"' "$SRC_DIR/config.example.json" 2>/dev/null | grep -oE '"[0-9]{1,3}(\.[0-9]{1,3}){3}"' | head -1 | tr -d '"')"
MQTT_HOST="${MQTT_HOST:-116.204.32.197}"
if timeout 3 bash -c "echo > /dev/tcp/$MQTT_HOST/1883" 2>/dev/null; then
    info "MQTT broker $MQTT_HOST:1883 可达"
else
    warn "MQTT broker $MQTT_HOST:1883 不可达（3s 超时）——外网/防火墙问题，程序会自动重连"
fi

MEM_LINE="$(free -h 2>/dev/null | sed -n 2p)"
[ -n "$MEM_LINE" ] && info "内存: $MEM_LINE （本机编译建议 1G+ 可用，不够就 JOBS=1）"
DISK_LINE="$(df -h "$SRC_DIR" 2>/dev/null | sed -n 2p)"
[ -n "$DISK_LINE" ] && info "磁盘: $DISK_LINE （编译约需 200MB）"

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
    echo " 结果: FAIL —— 按上面 [FAIL] 逐条修"
    exit 1
fi
