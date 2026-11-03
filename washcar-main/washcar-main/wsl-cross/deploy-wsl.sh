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
#   BOX_SSH_KEY=~/.ssh/id_ed25519 用 SSH key 登录（推荐；支持 ssh-agent）
#   BOX_PASS=xxxx                 没有 key 时的 SSH 密码（用 sshpass -e，不进命令行）
#   BOX_DEST_DIR=~/washcar/deployed  目标目录
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BOX_HOST="${BOX_HOST:-ztl@192.168.31.248}"
BOX_PASS="${BOX_PASS:-}"
BOX_SSH_KEY="${BOX_SSH_KEY:-}"
BOX_DEST_DIR="${BOX_DEST_DIR:-/home/ztl/washcar/deployed}"
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
BINARY="${BUILD_DIR}/main"

RKN_LIBS=(
    "${PROJECT_ROOT}/lib/rknn/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
    "${PROJECT_ROOT}/lib/rknn/3rdparty/librga/Linux/aarch64/librga.so"
)

SSH_OPTS=(-o StrictHostKeyChecking=no -o ConnectTimeout=10)
if [ -n "${BOX_SSH_KEY}" ]; then
    SSH_CMD=(ssh "${SSH_OPTS[@]}" -i "${BOX_SSH_KEY}")
    SCP_CMD=(scp "${SSH_OPTS[@]}" -i "${BOX_SSH_KEY}")
elif [ -n "${BOX_PASS}" ]; then
    # -e 从 SSHPASS 环境变量读密码, 避免密码出现在 ps/命令行里
    export SSHPASS="${BOX_PASS}"
    SSH_CMD=(sshpass -e ssh "${SSH_OPTS[@]}")
    SCP_CMD=(sshpass -e scp "${SSH_OPTS[@]}")
else
    echo "ERROR: 需要 BOX_SSH_KEY 或 BOX_PASS。推荐:" >&2
    echo "  BOX_SSH_KEY=~/.ssh/id_ed25519 bash wsl-cross/deploy-wsl.sh" >&2
    echo "  BOX_PASS=xxx bash wsl-cross/deploy-wsl.sh" >&2
    exit 1
fi

if [ ! -f "${BINARY}" ]; then
    echo "ERROR: ${BINARY} not found. Run build-fast.sh first." >&2
    exit 1
fi

# 防止 deploy 把 build-fast 之前的旧根产物发上去: build-fast 会把新产物复制到根目录。
if [ -f "${BUILD_DIR}/src/main" ] && [ "${BUILD_DIR}/src/main" -nt "${BINARY}" ]; then
    echo "ERROR: ${BINARY} 比 src/main 旧, 可能是旧产物; 先重新跑 build-fast.sh" >&2
    exit 6
fi
if ! file "${BINARY}" | grep -q "ARM aarch64"; then
    echo "ERROR: ${BINARY} 不是 ARM aarch64 产物" >&2
    file "${BINARY}" >&2
    exit 5
fi

echo ">>> 目标盒子: ${BOX_HOST}"
echo ">>> 产物: $(du -h "${BINARY}" | cut -f1)  $(file "${BINARY}" | grep -o 'ARM aarch64')"

echo ""
echo ">>> [1/5] 创建目标目录..."
"${SSH_CMD[@]}" "${BOX_HOST}" "mkdir -p ${BOX_DEST_DIR}"

echo ">>> [2/5] 传输二进制 + 运行时 .so + web/"
"${SCP_CMD[@]}" "${BINARY}" "${BOX_HOST}:${BOX_DEST_DIR}/main"
for so in "${RKN_LIBS[@]}"; do
    if [ ! -f "${so}" ]; then
        echo "ERROR: 缺少 ${so}，仓库里的 rknn 库不完整" >&2
        exit 4
    fi
    "${SCP_CMD[@]}" "${so}" "${BOX_HOST}:${BOX_DEST_DIR}/$(basename "${so}")"
done
"${SSH_CMD[@]}" "${BOX_HOST}" "chmod +x ${BOX_DEST_DIR}/main"

if [ -d "${PROJECT_ROOT}/web" ]; then
    "${SCP_CMD[@]}" -r "${PROJECT_ROOT}/web" "${BOX_HOST}:${BOX_DEST_DIR}/"
fi

echo ">>> [3/5] ldd 依赖校验..."
LDD_OUTPUT=$("${SSH_CMD[@]}" "${BOX_HOST}" "cd ${BOX_DEST_DIR} && ldd ./main" 2>&1) || true
echo "${LDD_OUTPUT}"
if echo "${LDD_OUTPUT}" | grep -q 'not found'; then
    echo ""
    echo "ERROR: 盒子上有缺失的依赖库！" >&2
    exit 3
fi
echo ">>> 所有依赖库均已解析 ✓"

echo ""
echo ">>> [4/5] 盒子 glibc 版本:"
"${SSH_CMD[@]}" "${BOX_HOST}" "ldd --version | head -1"

if [ "${1:-}" = "run" ]; then
    echo ""
    echo ">>> [5/5] 启动运行 3 秒..."
    "${SSH_CMD[@]}" -t "${BOX_HOST}" "cd ${BOX_DEST_DIR} && timeout 3 ./main 2>&1 || true"
fi

echo ""
echo ">>> 部署完成！"
echo "    运行: ssh ${BOX_HOST} ${BOX_DEST_DIR}/main"
echo "    依赖: ssh ${BOX_HOST} ldd ${BOX_DEST_DIR}/main"
