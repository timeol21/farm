#!/bin/bash
# =============================================================================
# WSL 交叉编译环境一键初始化（RK3568 / aarch64 / Ubuntu 20.04 glibc 2.31）
#
# 在 WSL Ubuntu-22.04 中执行（需要 root / sudo）。
# 做的事：
#   1. 配置 apt 多架构：amd64 用 jammy，arm64 用 focal（对齐盒子 glibc 2.31）
#   2. 安装 gcc-9 aarch64 交叉编译器 + cmake/ninja/ccache/sshpass
#   3. 下载 focal arm64 开发库（openssl/ffmpeg/sqlite 等）到独立 sysroot
#   4. 修复 sysroot 里的 .so 符号链接（替换为实际 ELF 文件，避免 ld 死循环）
#   5. 安装 nlohmann-json / libstdc++-9 / OpenCL 到 sysroot
#
# 用法：sudo bash wsl-cross/setup-env.sh
# =============================================================================
set -euo pipefail

SYSROOT=/opt/rk3568-sysroot
DEB_DIR=/tmp/rk3568-debs

echo "=========================================="
echo " WSL RK3568 交叉编译环境初始化"
echo "=========================================="

# ---------- 1. apt 源配置 ----------
echo ""
echo "[1/6] 配置 apt 多架构源..."
dpkg --add-architecture arm64

# 给现有 jammy 源加 arch=amd64 限定
sed -i 's/^deb /deb [arch=amd64] /' /etc/apt/sources.list

# arm64 用 focal ubuntu-ports（对齐盒子 glibc 2.31）
cat > /etc/apt/sources.list.d/arm64-focal.list << 'EOF'
deb [arch=arm64] http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/ focal main restricted universe multiverse
deb [arch=arm64] http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/ focal-updates main restricted universe multiverse
deb [arch=arm64] http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/ focal-security main restricted universe multiverse
deb [arch=arm64] http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/ focal-backports main restricted universe multiverse
EOF

apt-get update -qq

# ---------- 2. 安装交叉编译器和主机工具 ----------
echo ""
echo "[2/6] 安装 gcc-9 交叉编译器和构建工具..."
apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    pkg-config \
    rsync \
    file \
    sshpass \
    gcc-9-aarch64-linux-gnu \
    g++-9-aarch64-linux-gnu

# ---------- 3. 下载 arm64 开发库到独立 sysroot ----------
echo ""
echo "[3/6] 下载 focal arm64 开发库到 $SYSROOT ..."
rm -rf "$SYSROOT" "$DEB_DIR"
mkdir -p "$SYSROOT" "$DEB_DIR"

TOP_PKGS='libc6-dev libssl-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libsqlite3-dev'

# BFS 递归解析依赖
declare -A SEEN
QUEUE=($TOP_PKGS)
ALL_PKGS=''
while [ ${#QUEUE[@]} -gt 0 ]; do
    PKG="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")
    PKG="${PKG%%:*}"
    [ -n "${SEEN[$PKG]:-}" ] && continue
    SEEN[$PKG]=1
    ALL_PKGS="$ALL_PKGS $PKG"
    DEPS=$(apt-cache depends "$PKG:arm64" 2>/dev/null \
        | grep -E '^  (Depends|PreDepends):' \
        | sed 's/.*: //' | grep -v '|' | grep -v '<' | sed 's/:arm64//' | sort -u)
    for D in $DEPS; do
        D="${D%%:*}"
        [ -z "${SEEN[$D]:-}" ] && QUEUE+=("$D")
    done
done
echo "  共 $(echo $ALL_PKGS | wc -w) 个包"

cd "$DEB_DIR"
for PKG in $ALL_PKGS; do
    apt-get download "$PKG:arm64" 2>/dev/null || echo "  WARN: download failed: $PKG"
done

for DEB in "$DEB_DIR"/*.deb; do
    [ -f "$DEB" ] && dpkg -x "$DEB" "$SYSROOT"
done
echo "  sysroot 初始大小: $(du -sh $SYSROOT | cut -f1)"

# ---------- 4. 额外包：nlohmann-json / libstdc++-9 / OpenCL ----------
echo ""
echo "[4/6] 安装 nlohmann-json / libstdc++-9 / OpenCL 到 sysroot..."
cd /tmp
apt-get download nlohmann-json3-dev 2>/dev/null
dpkg -x nlohmann-json3-dev_*.deb "$SYSROOT/" 2>/dev/null || true

apt-get download libstdc++-9-dev:arm64 2>/dev/null
dpkg -x libstdc++-9-dev_*.deb "$SYSROOT/" 2>/dev/null || true

apt-get download ocl-icd-libopencl1:arm64 2>/dev/null
dpkg -x ocl-icd-libopencl1_*.deb "$SYSROOT/" 2>/dev/null || true

# ---------- 5. 修复 .so 符号链接 ----------
echo ""
echo "[5/6] 修复 sysroot .so 符号链接（替换为实际 ELF 文件）..."
LIBDIR=$SYSROOT/usr/lib/aarch64-linux-gnu
ROOTLIBDIR=$SYSROOT/lib/aarch64-linux-gnu

# 基础库：从 /lib/aarch64-linux-gnu/ 复制实际 .so 文件
for lib in pthread dl rt m resolv; do
    actual=$(ls "$ROOTLIBDIR/lib${lib}-"*.so 2>/dev/null | head -1)
    if [ -n "$actual" ]; then
        rm -f "$LIBDIR/lib${lib}.so"
        cp "$actual" "$LIBDIR/lib${lib}.so"
    fi
done

# libcrypt
[ -f "$ROOTLIBDIR/libcrypt.so.1" ] && cp "$ROOTLIBDIR/libcrypt.so.1" "$LIBDIR/libcrypt.so"

# libstdc++：用 sysroot 里的 20.04 版本（6.0.28），不用交叉编译器自带的
if [ -f "$LIBDIR/libstdc++.so.6.0.28" ]; then
    cp "$LIBDIR/libstdc++.so.6.0.28" "$LIBDIR/libstdc++.so"
fi

# libgcc_s
[ -f "$ROOTLIBDIR/libgcc_s.so.1" ] && cp "$ROOTLIBDIR/libgcc_s.so.1" "$LIBDIR/libgcc_s.so"

echo "  修复完成"

# ---------- 6. 验证 ----------
echo ""
echo "[6/6] 验证环境..."
echo "  交叉编译器: $(aarch64-linux-gnu-gcc-9 --version | head -1)"
echo "  sysroot glibc: $(aarch64-linux-gnu-readelf -d $ROOTLIBDIR/libc.so.6 2>/dev/null | grep SONAME || echo 'ok')"
echo "  libstdc++: $(file $LIBDIR/libstdc++.so | head -1)"
echo "  libssl: $(ls -la $LIBDIR/libssl.so 2>/dev/null | awk '{print $NF}')"

# 简单链接测试
echo 'extern int pthread_create(void*,void*,void*,void*); int main(){pthread_create(0,0,0,0);return 0;}' > /tmp/_t.c
if aarch64-linux-gnu-gcc-9 --sysroot="$SYSROOT" -L"$LIBDIR" -L"$ROOTLIBDIR" /tmp/_t.c -o /tmp/_t -lpthread 2>/dev/null; then
    echo "  链接测试: PASS"
    rm -f /tmp/_t /tmp/_t.c
else
    echo "  链接测试: FAILED（请检查上方输出）"
fi

echo ""
echo "=========================================="
echo " 环境初始化完成！"
echo " sysroot: $SYSROOT ($(du -sh $SYSROOT | cut -f1))"
echo " 编译器: aarch64-linux-gnu-gcc-9 / g++-9"
echo "=========================================="
