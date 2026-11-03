#!/bin/bash
# 盒子本机编译 + 组装运行目录（源码已在盒子上时的部署路线）
# 用法: bash build-on-box.sh          编译 + 组装 + 校验
#       bash build-on-box.sh run      再冒烟跑 5 秒
# 可用环境变量: JOBS=2 RUN_DIR=/home/ztl/washcar/deployed BUILD_DIR=...
set -euo pipefail

find_repo_root() {
    local d="$1"
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        [ -d "$d/lib/rknn/3rdparty" ] && { echo "$d"; return 0; }
        d="$(dirname "$d")"
    done
    return 1
}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SRC_DIR:-$(find_repo_root "$SCRIPT_DIR" || find_repo_root "$PWD")}"
BUILD="${BUILD_DIR:-$SRC/build-box}"
#RUN_DIR="${RUN_DIR:-/home/ztl/washcar/deployed}"
RUN_DIR="${RUN_DIR:-/home/ztl/program/washcar-main/washcar-main/deployed}"
JOBS="${JOBS:-2}"    # 项目文档提醒本机编译易 OOM，默认 2 路；内存充裕可 JOBS=4

echo ">>> 源码: $SRC"
echo ">>> 构建: $BUILD"
echo ">>> 运行: $RUN_DIR"

# 1) configure。关键点：RUNPATH=$ORIGIN —— wsl-cross 工具链里那段 rpath 配置
#    只在交叉编译时生效，本机编译必须在这里补上，否则 ./main 找不到
#    同目录的 librknnrt.so / librga.so（项目注释里实测过这个坑）。
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_RPATH='$ORIGIN' \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON

# 2) 只编 main 这个 target（十来个 *_test target 不编，省时省内存）
cmake --build "$BUILD" --target main -j"$JOBS"

# 3) 组装运行目录：main + 两个运行时 .so + 模型。
#    程序启动后会 chdir 到 exe 所在目录，再按相对路径 lib/rknn/models/ 找模型，
#    所以目录结构必须保持这样，不能把模型平铺到 RUN_DIR 根下。
mkdir -p "$RUN_DIR/lib/rknn/models"
cp "$BUILD/src/main" "$RUN_DIR/main"
chmod +x "$RUN_DIR/main"
cp "$SRC/lib/rknn/3rdparty/rknpu2/Linux/aarch64/librknnrt.so" "$RUN_DIR/"
cp "$SRC/lib/rknn/3rdparty/librga/Linux/aarch64/librga.so"     "$RUN_DIR/"
cp "$SRC"/lib/rknn/models/*.rknn "$RUN_DIR/lib/rknn/models/"

# 4) 校验 RUNPATH + ldd
echo ">>> RUNPATH 校验:"
readelf -d "$RUN_DIR/main" | grep -E 'RPATH|RUNPATH' \
    || echo "  警告: 没有 RUNPATH，运行时需 LD_LIBRARY_PATH=. ./main 兜底"
echo ">>> ldd 校验:"
MISS="$(cd "$RUN_DIR" && ldd ./main 2>&1 | grep 'not found' || true)"
if [ -n "$MISS" ]; then
    echo "$MISS"; echo "ERROR: 有缺失依赖"; exit 1
fi
echo "  全部依赖可解析"

echo ">>> 完成。手工运行: cd $RUN_DIR && ./main"
echo ">>> 调试版(带符号): $BUILD/src/main"
echo ">>> 开机自启(可选): cd $SRC/deploy && bash install.sh"

# 5) 可选冒烟
if [ "${1:-}" = "run" ]; then
    echo ">>> 冒烟运行 5 秒..."
    (cd "$RUN_DIR" && timeout 5 ./main) || true
fi