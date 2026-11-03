#!/bin/bash
# =============================================================================
# 盒子本机编译 + 组装运行目录（路线 A：源码上传，盒子自己编译）
#
# 用法:
#   bash build-on-box.sh             编译 + 组装 + 校验
#   bash build-on-box.sh run         再冒烟跑 5 秒
#   bash build-on-box.sh clean       删掉 build-box/ 和 deployed/ 重新来
#
# 可用环境变量:
#   JOBS=4                  并行编译数（默认 2，盒子内存紧张；4G 以上可调 4）
#   BUILD_DIR=<自定义>      构建目录（默认 <项目>/build-box）
#   RUN_DIR=<自定义>        运行目录（默认 <项目>/deployed）
#
# 产物位置（全在项目内，与 washcar-main / washcar-val 天然隔离）:
#   <项目>/build-box/main    带符号的调试版
#   <项目>/deployed/main     部署版（strip 过）+ libmosquitto.so.1
#
# 和 washcar-val 的 build-on-box.sh 的区别:
#   没有 rknn / rga / 模型文件 —— 精简版不含摄像头和 AI。
#   多了 libmosquitto.so.1 —— RUNPATH=$ORIGIN 要在 main 同目录找到它。
#   依赖从 third_party/ 取，盒子上不需要 apt install libmosquitto-dev。
# =============================================================================
set -euo pipefail

# 项目根识别: 往上找，第一个含 src/ 目录的层就是项目根。
# 用 src/ 作判据而不是 lib/rknn/3rdparty —— 精简版根本没有 lib/rknn。
find_repo_root() {
    local d="$1"
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        [ -d "$d/src" ] && { echo "$d"; return 0; }
        d="$(dirname "$d")"
    done
    return 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SRC_DIR:-$(find_repo_root "$SCRIPT_DIR" || find_repo_root "$PWD")}"
BUILD="${BUILD_DIR:-$SRC/build-box}"
RUN_DIR="${RUN_DIR:-$SRC/deployed}"
JOBS="${JOBS:-3}"

die() { echo "ERROR: $*" >&2; exit 1; }

# 找仓库内置的 libmosquitto 实体文件。
# 不能用固定文件名——Windows 上传/zip 解压后 third_party 里的 libmosquitto.so 和
# libmosquitto.so.1 这两个软链会丢，只剩 libmosquitto.so.1.6.9。所以按目录 glob，
# 短名字优先（.so > .so.1 > .so.1.6.9），谁在就用谁。
find_vendor_mosq() {
    local d f
    for d in "$SRC/third_party/mosquitto/lib/aarch64" "$SRC/third_party/mosquitto/lib"; do
        [ -d "$d" ] || continue
        for f in "$d/libmosquitto.so" "$d/libmosquitto.so.1" "$d"/libmosquitto.so.*; do
            [ -s "$f" ] && { echo "$f"; return 0; }   # -s: 跳过 0 字节空壳
        done
    done
    return 1
}

# ---------- clean 子命令 ----------
if [ "${1:-}" = "clean" ]; then
    echo ">>> 删除 $BUILD 和 $RUN_DIR"
    rm -rf "$BUILD" "$RUN_DIR"
    echo ">>> 已清理。重新编译: bash build-on-box.sh"
    exit 0
fi

echo "============================================="
echo " washcar-test 盒子本机编译"
echo " 源码: $SRC"
echo " 构建: $BUILD"
echo " 运行: $RUN_DIR"
echo " 并行: JOBS=$JOBS"
echo "============================================="

# ---------- 前置检查（失败尽早失败，别等到链接）----------
[ -n "$SRC" ] || die "找不到项目根（向上找不到含 src/ 的目录）。用 SRC_DIR=<路径> 指定"

ARCH="$(uname -m)"
[ "$ARCH" = "aarch64" ] || echo ">>> 警告: 当前架构 $ARCH，产物是给本机用的；RK3568 上应为 aarch64"

command -v cmake >/dev/null 2>&1 \
    || die "没有 cmake。装: sudo apt-get install -y cmake"
command -v g++ >/dev/null 2>&1 \
    || die "没有 g++。装: sudo apt-get install -y build-essential"
[ -d "$SRC/src" ] || die "$SRC/src 不存在，源码不完整"

# mosquitto 运行库: 盒子上一般不装 libmosquitto1，所以用仓库内置的 aarch64 副本。
# 本机架构不是 aarch64（比如在 x86 上验证脚本）时改用系统库，否则会把 aarch64 的 .so
# 拷到 x86 的部署目录里，ldd 直接报 wrong ELF class。
MOSQ_SO_SRC=""
if [ "$ARCH" = "aarch64" ]; then
    MOSQ_SO_SRC="$(find_vendor_mosq)" || die "third_party/mosquitto/lib/aarch64 下找不到 libmosquitto.so* —— 仓库没传全，重新上传 third_party/"
    echo ">>> 内置 libmosquitto: ${MOSQ_SO_SRC#$SRC/}"
else
    MOSQ_SO_SRC="$(ldconfig -p 2>/dev/null | awk '/libmosquitto\.so\.1 /{print $NF; exit}')"
    [ -n "$MOSQ_SO_SRC" ] && [ -f "$MOSQ_SO_SRC" ] \
        || die "非 aarch64 架构且系统没装 libmosquitto1。装: sudo apt-get install -y libmosquitto-dev"
    echo ">>> 注意: 架构 $ARCH 不是 aarch64，改用系统 libmosquitto: $MOSQ_SO_SRC"
fi

# 编译期能不能找到 mosquitto.h 交给 CMake 判断（它会回退到 third_party）
echo ">>> 检查用到的编译器: $(g++ --version | head -1)"

# ---------- 1) configure ----------
# RUNPATH=$ORIGIN: 让 ./main 在自己所在目录找 libmosquitto.so.1。
# 交叉编译时这段配置在 wsl-cross/toolchain-*.cmake 里，本机编译必须在这里补，
# 否则部署完 ./main 会报 "error while loading shared libraries: libmosquitto.so.1"
# （CMakeLists.txt 里也设了默认值，这里再显式传一次，双保险）
echo ""
echo ">>> [1/4] cmake configure..."
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_RPATH='$ORIGIN' \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON || {
        echo "" >&2
        echo "提示: 上面如果是「找不到 mosquitto」，先看 third_party 里到底有没有库:" >&2
        echo "      ls -l $SRC/third_party/mosquitto/lib/aarch64/" >&2
        echo "      若为空 → 上传源码时 third_party/ 没传全，重传一次（保持目录结构）" >&2
        echo "      若上次是旧版本编过 → bash build-on-box.sh clean 清掉 CMake 缓存再试" >&2
        die "cmake configure 失败"
    }

# ---------- 2) build ----------
echo ""
echo ">>> [2/4] 编译 main (JOBS=$JOBS)..."
cmake --build "$BUILD" --target main -j"$JOBS" || die "编译失败"

[ -f "$BUILD/src/main" ] || die "编译完成但找不到 $BUILD/src/main"

# ---------- 3) 组装运行目录 ----------
# 精简版的运行目录只有两样东西: main + libmosquitto.so.1
# 程序按 /proc/self/exe 定位自己的目录，从同目录的 config.json 读配置、写 data/log/。
echo ""
echo ">>> [3/4] 组装 $RUN_DIR ..."
mkdir -p "$RUN_DIR"
cp "$BUILD/src/main" "$RUN_DIR/main"
chmod +x "$RUN_DIR/main"

# NEEDED 里写的是 libmosquitto.so.1，所以目标文件名必须是这个（不能是 .so.1.6.9）
cp -L "$MOSQ_SO_SRC" "$RUN_DIR/libmosquitto.so.1"

# 配置文件：项目根有就用项目的，没有则等首次运行自动生成
if [ -f "$SRC/config.json" ]; then
    cp "$SRC/config.json" "$RUN_DIR/config.json"
    echo "     已带上 config.json"
elif [ -f "$SRC/config.example.json" ]; then
    echo "     注意: 项目根没有 config.json，首次运行会用内置默认值生成模板"
fi

# strip 一份部署版，体积对折；build-box 里保留带符号的用于 gdb
if command -v strip >/dev/null 2>&1; then
    BEFORE=$(du -h "$RUN_DIR/main" | cut -f1)
    strip "$RUN_DIR/main" 2>/dev/null || true
    AFTER=$(du -h "$RUN_DIR/main" | cut -f1)
    echo "     strip: $BEFORE -> $AFTER"
fi

# ---------- 4) 校验 ----------
echo ""
echo ">>> [4/4] 校验"
echo "     RUNPATH:"
readelf -d "$RUN_DIR/main" 2>/dev/null | grep -E 'RPATH|RUNPATH' \
    | sed 's/^/       /' \
    || echo "       警告: 没有 RUNPATH，运行时需 LD_LIBRARY_PATH=. ./main 兜底"

echo "     ldd:"
if ! ( cd "$RUN_DIR" && ldd ./main >"$BUILD/washcar-ldd.txt" 2>&1 ); then
    cat "$BUILD/washcar-ldd.txt" | sed 's/^/       /'
    die "ldd 执行失败"
fi
MISS="$(grep 'not found' "$BUILD/washcar-ldd.txt" || true)"
if [ -n "$MISS" ]; then
    echo "$MISS" | sed 's/^/       /'
    die "有缺失依赖（盒子缺系统库，或 libmosquitto.so.1 没放到 main 同级目录）"
fi
echo "       全部依赖可解析 ✓"
sed 's/^/       /' "$BUILD/washcar-ldd.txt" | head -6

echo ""
echo "============================================="
echo " 完成"
echo "   运行:  cd $RUN_DIR && ./main"
echo "   调试版(带符号): $BUILD/src/main"
echo "============================================="

# ---------- 可选冒烟 ----------
if [ "${1:-}" = "run" ]; then
    echo ""
    echo ">>> 冒烟运行 5 秒（缺 PLC / MQTT broker 属预期，能打印启动流程即正常）..."
    ( cd "$RUN_DIR" && timeout 5 ./main 2>&1 | head -30 ) || true
fi
