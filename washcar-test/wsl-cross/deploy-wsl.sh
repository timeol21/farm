#!/usr/bin/env bash
# =============================================================================
# 部署 washcar-test 到 RK3568 盒子
#
# 用法（在 WSL 中，项目根目录下执行）：
#   bash wsl-cross/deploy-wsl.sh              # scp + ldd 校验
#   bash wsl-cross/deploy-wsl.sh run          # 再冒烟跑 5 秒
#
# 环境变量：
#   BOX_HOST=ztl@192.168.31.248      目标盒子
#   BOX_PASS=xxxx                    SSH 密码（必填，不要写进仓库）
#   BOX_DEST_DIR=/home/ztl/program/washcar-test/deployed   目标目录
#   SYSROOT=/opt/rk3568-sysroot      交叉编译 sysroot（取 libmosquitto）
#
# 部署内容（比 washcar-val 简单，因为没有 rknn/rga/模型）：
#   main                  可执行
#   libmosquitto.so.1     MQTT 库（盒子通常没装，随包带）
#   config.json           存在才传
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BOX_HOST="${BOX_HOST:-ztl@192.168.31.248}"
BOX_PASS="${BOX_PASS:-}"
# 盒子上的落点。本地一律用项目根下的 deployed/，与 build-wsl.sh / build-on-box.sh 一致。
# 盒子侧也落在**源码目录内**的 deployed/，不要往 ~ 根目录另开一份：
# 之前默认值写成 /home/ztl/washcar-test/deployed，跟文档里的 ~/program/washcar-test
# 不一致，结果盒子上出现两套程序，跑起来的是哪一套说不清。
BOX_DEST_DIR="${BOX_DEST_DIR:-/home/ztl/program/washcar-test/deployed}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build-wsl}"
DEPLOY_DIR="${DEPLOY_DIR:-${PROJECT_ROOT}/deployed}"
SYSROOT="${SYSROOT:-/opt/rk3568-sysroot}"
# 优先用 deployed/（build-wsl.sh 的最终产物）；老流程只编不组装时回退 build-wsl/
if [ -f "${DEPLOY_DIR}/main" ]; then
    BINARY="${DEPLOY_DIR}/main"
else
    BINARY="${BUILD_DIR}/main"
fi

# 运行时要一起带过去的库。RUNPATH=$ORIGIN 会从 main 同目录加载它们。
# libmosquitto: Ubuntu 盒子上一般没有，必须随包带，否则 ./main 直接起不来。
# 优先用仓库内置的副本（third_party/），保证「交叉编译」和「盒子本机编译」用的是
# 同一个版本的库；sysroot 里取只是兜底。
# 内置副本不写死文件名: 从 Windows 拷贝过来后，.so / .so.1 这两个软链常常会丢，
# 只剩实体 .so.1.6.9。按目录 glob，短名字优先，谁在就用谁。
MOSQ_SO=""
for _d in "${PROJECT_ROOT}/third_party/mosquitto/lib/aarch64" "${PROJECT_ROOT}/third_party/mosquitto/lib"; do
    [ -d "$_d" ] || continue
    for _f in "$_d/libmosquitto.so" "$_d/libmosquitto.so.1" "$_d"/libmosquitto.so.*; do
        [ -s "$_f" ] && { MOSQ_SO="$_f"; break; }   # -s: 跳过 0 字节空壳
    done
    [ -n "$MOSQ_SO" ] && break
done
if [ -z "$MOSQ_SO" ]; then
    for _f in "${SYSROOT}/usr/lib/aarch64-linux-gnu/libmosquitto.so"*; do
        [ -s "$_f" ] && { MOSQ_SO="$_f"; break; }   # -s: 跳过 0 字节空壳
    done
fi

SSHPASS="sshpass -p ${BOX_PASS}"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY 不存在，先在 WSL 里跑: bash wsl-cross/build-wsl.sh" >&2
    exit 1
fi
if [ -z "$BOX_PASS" ]; then
    echo "ERROR: 未设置 BOX_PASS。用法: BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh" >&2
    exit 1
fi
if [ ! -e "$MOSQ_SO" ]; then
    echo "ERROR: 找不到 libmosquitto" >&2
    echo "       期望位置: ${PROJECT_ROOT}/third_party/mosquitto/lib/aarch64/libmosquitto.so*" >&2
    echo "       兜底位置: ${SYSROOT}/usr/lib/aarch64-linux-gnu/libmosquitto.so*" >&2
    echo "       仓库里 third_party 没传全时，用 sudo apt-get install -y libmosquitto-dev:arm64 装进 sysroot" >&2
    exit 4
fi

echo ">>> 目标盒子: ${BOX_HOST}"
echo ">>> 产物: $(du -h "$BINARY" | cut -f1)  $(file "$BINARY" | grep -o 'ARM aarch64')"

echo ""
echo ">>> [1/5] 创建目标目录..."
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "mkdir -p ${BOX_DEST_DIR}"

echo ">>> [2/5] 传输可执行 + MQTT 库..."
$SSHPASS scp $SSH_OPTS "$BINARY" "${BOX_HOST}:${BOX_DEST_DIR}/main"
# -L 解引用符号链接，拷实际的 .so.1.6.9 但保留 libmosquitto.so.1 这个名字
# （二进制 NEEDED 里写的就是 libmosquitto.so.1）
$SSHPASS scp -L $SSH_OPTS "$MOSQ_SO" "${BOX_HOST}:${BOX_DEST_DIR}/libmosquitto.so.1"

if [ -f "${PROJECT_ROOT}/config.json" ]; then
    echo "     (同时传 config.json)"
    $SSHPASS scp $SSH_OPTS "${PROJECT_ROOT}/config.json" "${BOX_HOST}:${BOX_DEST_DIR}/config.json"
else
    echo "     注意: 项目根没有 config.json，盒子首次启动会用内置默认值生成模板"
fi

$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "chmod +x ${BOX_DEST_DIR}/main"

echo ""
echo ">>> [3/5] 目标目录内容:"
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "cd ${BOX_DEST_DIR} && ls -lh | sed 's/^/    /'"

echo ""
echo ">>> [4/5] ldd 依赖校验..."
LDD_OUTPUT=$($SSHPASS ssh $SSH_OPTS "$BOX_HOST" "cd ${BOX_DEST_DIR} && ldd ./main" 2>&1)
echo "$LDD_OUTPUT" | sed 's/^/    /'
if echo "$LDD_OUTPUT" | grep -q 'not found'; then
    echo ""
    echo "ERROR: 盒子上有缺失的依赖库！" >&2
    exit 3
fi
echo "    >>> 所有依赖库均已解析 ✓"

echo ""
echo ">>> 盒子 glibc 版本:"
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "ldd --version | head -1" | sed 's/^/    /'

if [ "${1:-}" = "run" ]; then
    echo ""
    echo ">>> [5/5] 冒烟运行 5 秒..."
    $SSHPASS ssh $SSH_OPTS -t "$BOX_HOST" "cd ${BOX_DEST_DIR} && timeout 5 ./main 2>&1 || true"
fi

echo ""
echo ">>> 部署完成"
echo "    运行: ssh ${BOX_HOST}"
echo "          cd ${BOX_DEST_DIR} && ./main"
