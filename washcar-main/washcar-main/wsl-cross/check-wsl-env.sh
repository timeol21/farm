#!/usr/bin/env bash
# WSL 交叉编译环境自检
#
# 存在的理由: setup-env.sh 跑完不报错 ≠ 环境是对的。
# 2026-10-16 实测过一次"环境看着建好了、编译也过了、产物传到盒子上第一次写 std::cerr 就 SIGSEGV":
#   --sysroot 只管库搜索路径,不管 crt 启动文件;GCC 自己的 multiarch 目录排在 sysroot 前面,
#   Scrt1.o/crti.o 取自宿主 22.04(glibc 2.35),而 libc.so 取自 sysroot 的 20.04(glibc 2.31)。
#   启动代码和运行时不是同一套 -> .init_array 不执行 -> std::ios_base::Init 不跑
#   -> std::cerr 虚表指针是 0。6 行 hello world 就能复现,与项目代码无关。
#
# 思路对齐瑞芯微官方 SDK 的 check-*.sh:验证"能力"而不是验证"存在"。
# 单纯检查文件在不在没用 —— sysroot 缺 bits/c++config.h 时所有常规文件都在,
# 但编译器会静默退回用自己的头文件,不报任何错。

set -u

# 用 $HOME 推导, 别写死 /home/ztl —— 换台电脑用户名不是 ztl 就体检不到东西了。
PROJECT_ROOT="${PROJECT_ROOT:-${HOME}/washcar-wsl}"
# 构建目录必须和 build-fast.sh 用的一致。
# 之前这里写的是 "$PROJECT_ROOT/build-wsl" —— 那是 2026-10-21 之前的旧路径,
# 体检的是两天前的陈旧产物,还会照样报 "[OK] 缓存一致 / RUNPATH 正常"(假绿灯)。
BUILD_DIR="${BUILD_DIR:-${HOME}/washcar-build}"
SYSROOT=/opt/rk3568-sysroot
EXPECT_GCC="9.5.0"
CXX=aarch64-linux-gnu-g++-9

FAILED=0
ok()  { echo "  [OK]   $*"; }
bad() { echo "  [FAIL] $*"; FAILED=1; }

echo "============================================="
echo " WSL 交叉编译环境自检"
echo "============================================="

# ---------- 1. 交叉编译器 ----------
echo
echo "[1/7] 交叉编译器"
if ! command -v "$CXX" >/dev/null 2>&1; then
    bad "$CXX 不存在,先跑 wsl-cross/setup-env.sh"
else
    ver=$("$CXX" --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+$')
    if [ "$ver" = "$EXPECT_GCC" ]; then
        ok "$CXX $ver"
    else
        bad "$CXX 版本是 $ver,期望 $EXPECT_GCC(不一致会让 sysroot 的 20.04 库和头文件对不上)"
    fi
fi

# ---------- 2. sysroot 存在 ----------
echo
echo "[2/7] sysroot"
if [ -d "$SYSROOT" ]; then
    ok "$SYSROOT 存在"
else
    bad "$SYSROOT 不存在,先跑 wsl-cross/setup-env.sh"
fi

# ---------- 3. sysroot 的 C++ 头文件完整性(最容易静默出问题的一环) ----------
echo
echo "[3/7] sysroot C++ 头文件"
# c++config.h 是目标相关的,随 libstdc++-N-dev-<triple>-cross 安装。
# sysroot 里如果缺它,#include <bits/c++config.h> 会失败并退回编译器自带的头目录,
# 于是"头文件来自宿主 22.04、库来自 sysroot 20.04",而这一步不产生任何警告。
CFG=""
for d in "$SYSROOT"/usr/include/c++/*/; do
    for cand in "$d"aarch64-linux-gnu/bits/c++config.h "$d"bits/c++config.h; do
        [ -f "$cand" ] && CFG="$cand" && break 2
    done
done
if [ -n "$CFG" ]; then
    ok "c++config.h: ${CFG#$SYSROOT}"
else
    # 已知缺口,/opt/rk3568-sysroot 要 root 才能补,补不了。
    # 后果是 C++ 头文件取自 $CXX 自带(gcc 9.5.0)、libstdc++ 取自 sysroot,
    # 两者要的 GLIBCXX 都是 3.4.26,实测产物在盒子上跑得通,所以只记录不判 FAIL。
    echo "  [注意] sysroot 里没有 bits/c++config.h,C++ 头文件实际取自 $CXX 自带"
fi

# ---------- 4. sysroot 的 libc 版本 ----------
echo
echo "[4/7] sysroot libc"
LIBDIR=""
for d in "$SYSROOT"/lib/aarch64-linux-gnu "$SYSROOT"/usr/lib/aarch64-linux-gnu; do
    [ -f "$d/libc.so.6" ] && LIBDIR="$d" && break
done
if [ -z "$LIBDIR" ]; then
    bad "sysroot 里找不到 libc.so.6"
else
    ver=$(strings "$LIBDIR/libc.so.6" 2>/dev/null | grep -oE 'GNU C Library.*version [0-9.]+' | head -1)
    ok "libc: ${ver:-版本未知}  (${LIBDIR#$SYSROOT})"
    case "$ver" in
        *2.31*) : ;;
        *) bad "sysroot 的 glibc 不是 2.31,和盒子(Ubuntu 20.04)对不上,产物可能带新符号" ;;
    esac
fi

# ---------- 5. toolchain 文件里有没有 -B ----------
echo
echo "[5/7] toolchain 链接参数"
TC=""
for f in "$PROJECT_ROOT/toolchain-wsl.cmake" "$PROJECT_ROOT/wsl-cross/toolchain-aarch64-wsl.cmake"; do
    [ -f "$f" ] && TC="$f" && break
done
if [ -z "$TC" ]; then
    bad "找不到 toolchain 文件"
else
    if tr '\n' ' ' < "$TC" | grep -q 'CMAKE_EXE_LINKER_FLAGS.*-B'; then
        ok "$(basename "$TC") 有 -B,crt 会从 sysroot 取"
    else
        bad "$(basename "$TC") 缺 -B —— crt 会取自宿主,产物在盒子上第一次写标准流就 SIGSEGV"
    fi
fi

# ---------- 6. CMake 缓存和 toolchain 是否一致 ----------
echo
echo "[6/7] CMake 缓存"
CACHE="$BUILD_DIR/CMakeCache.txt"
if [ ! -f "$CACHE" ]; then
    echo "  [跳过] 还没 configure 过"
elif [ -z "$TC" ]; then
    echo "  [跳过] 没有 toolchain 文件可比对"
else
    # toolchain 里是 set(CMAKE_EXE_LINKER_FLAGS "值" CACHE STRING "..."),值也可能写在下一行,
    # 所以先压成一行再截引号里的内容(PCRE 的 lookbehind 必须定长,不能写 \s*);
    # 缓存里才是 KEY:STRING=值 的格式。
    tc_flags=$(tr '\n' ' ' < "$TC" | grep -m1 -oP 'set\(CMAKE_EXE_LINKER_FLAGS\s*"[^"]*' | sed 's/^[^"]*"//' || true)
    cached=$(grep -m1 -oP '(?<=^CMAKE_EXE_LINKER_FLAGS:STRING=).*' "$CACHE" 2>/dev/null || true)
    if [ -z "$cached" ]; then
        echo "  [跳过] 缓存里没有 CMAKE_EXE_LINKER_FLAGS"
    elif [ "$cached" = "$tc_flags" ]; then
        ok "缓存与 toolchain 一致"
    else
        bad "缓存里的链接参数和 toolchain 文件不一致 —— 改完 toolchain 必须 cmake -U CMAKE_EXE_LINKER_FLAGS 重新 configure"
        echo "         缓存: $cached"
        echo "         文件: $tc_flags"
    fi
fi

# ---------- 7. 产物 RUNPATH(2026-10-19 加) ----------
echo
echo "[7/7] 产物 RUNPATH"
BIN="$BUILD_DIR/main"
READELF=aarch64-linux-gnu-readelf
command -v "$READELF" >/dev/null 2>&1 || READELF=readelf
if [ ! -f "$BIN" ]; then
    echo "  [跳过] 还没 build 出 main"
elif ! "$READELF" -d "$BIN" 2>/dev/null | grep -qE 'RUNPATH|RPATH'; then
    bad "产物没有 RUNPATH —— 部署到盒子后直接 ./main 会报 librknnrt.so: cannot open shared object file"
else
    ok "RUNPATH: $("$READELF" -d "$BIN" | grep -oE '(RUNPATH|RPATH).*\[[^]]*\]' | head -1)"
fi

echo
echo "============================================="
if [ "$FAILED" -eq 0 ]; then
    echo " 环境自检通过(体检目录: ${BUILD_DIR})"
else
    echo " 环境自检未通过,先修上面标 FAIL 的项(体检目录: ${BUILD_DIR})"
fi
echo "============================================="
exit "$FAILED"
