#!/usr/bin/env bash
# =============================================================================
# 部署 WSL 交叉编译产物到 RK3568 盒子并验证
#
# 用法（在 WSL 中，项目根目录下执行）：
#   bash wsl-cross/deploy-wsl.sh              # scp + ldd 校验
#   bash wsl-cross/deploy-wsl.sh run          # scp + ldd + 启动运行 3 秒
#
# 环境变量：
#   BOX_HOST=ztl@192.168.31.248   目标盒子（默认）
#   BOX_PASS=xxxx                   SSH 密码（必填，不写进仓库，用 sshpass）
#   BOX_DEST_DIR=~/washcar/deployed  目标目录
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BOX_HOST="${BOX_HOST:-ztl@192.168.31.248}"
BOX_PASS="${BOX_PASS:-}"
BOX_DEST_DIR="${BOX_DEST_DIR:-/home/ztl/washcar/deployed}"
# 构建目录跟 build-wsl.sh 保持一致(默认在 WSL 原生盘, 读 /mnt/c 慢一个数量级)
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
BINARY="${BUILD_DIR}/main"

# 跟二进制一起部署的运行时库(RUNPATH=$ORIGIN 找的就是它们,源码在仓库里,不用单独下载)
RKN_LIBS=(
    "${PROJECT_ROOT}/lib/rknn/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
    "${PROJECT_ROOT}/lib/rknn/3rdparty/librga/Linux/aarch64/librga.so"
)

SSHPASS="sshpass -p ${BOX_PASS}"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run build-wsl.sh first." >&2
    exit 1
fi

if [ -z "$BOX_PASS" ]; then
    echo "ERROR: 未设置 BOX_PASS。用 BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh 传入。" >&2
    exit 1
fi

echo ">>> 目标盒子: ${BOX_HOST}"
echo ">>> 产物: $(du -h "$BINARY" | cut -f1)  $(file "$BINARY" | grep -o 'ARM aarch64')"

echo ""
echo ">>> [1/5] 创建目标目录..."
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "mkdir -p ${BOX_DEST_DIR}"

echo ">>> [2/5] 传输二进制 + 运行时 .so..."
$SSHPASS scp $SSH_OPTS "$BINARY" "${BOX_HOST}:${BOX_DEST_DIR}/main"
# 产物 RUNPATH 是 $ORIGIN，运行时去二进制同级目录找这两个库，所以必须一起部署。
# 其余依赖（libcrypto / libav* / libc）盒子系统自带，不传。
for so in "${RKN_LIBS[@]}"; do
    if [ ! -f "$so" ]; then
        echo "ERROR: 缺少 $so，仓库里的 rknn 库不完整" >&2
        exit 4
    fi
    $SSHPASS scp $SSH_OPTS "$so" "${BOX_HOST}:${BOX_DEST_DIR}/$(basename "$so")"
done
# 从 Windows 盘(/mnt/c)或 umask 异常环境传过来的文件可能丢可执行位，盒子直接 ./main 会 Permission denied
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "chmod +x ${BOX_DEST_DIR}/main"

echo ">>> [3/5] ldd 依赖校验..."
LDD_OUTPUT=$($SSHPASS ssh $SSH_OPTS "$BOX_HOST" "cd ${BOX_DEST_DIR} && ldd ./main" 2>&1)
echo "$LDD_OUTPUT"
if echo "$LDD_OUTPUT" | grep -q 'not found'; then
    echo ""
    echo "ERROR: 盒子上有缺失的依赖库！" >&2
    exit 3
fi
echo ">>> 所有依赖库均已解析 ✓"

echo ""
echo ">>> [4/5] 盒子 glibc 版本:"
$SSHPASS ssh $SSH_OPTS "$BOX_HOST" "ldd --version | head -1"

if [ "${1:-}" = "run" ]; then
    echo ""
    echo ">>> [5/5] 启动运行 3 秒..."
    $SSHPASS ssh $SSH_OPTS -t "$BOX_HOST" "cd ${BOX_DEST_DIR} && timeout 3 ./main 2>&1 || true"
fi

echo ""
echo ">>> 部署完成！"
echo "    运行: ssh ${BOX_HOST} ${BOX_DEST_DIR}/main"
echo "    依赖: ssh ${BOX_HOST} ldd ${BOX_DEST_DIR}/main"
