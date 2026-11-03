# =============================================================================
# CMake toolchain: WSL 交叉编译 RK3568 (aarch64 / Ubuntu 20.04 glibc 2.31)
#
# 本文件的三个关键点：
#   - 使用独立 sysroot /opt/rk3568-sysroot（由 setup-env.sh 创建）
#   - 编译器固定 gcc-9（22.04 源里的 gcc-11 libstdc++ 依赖 glibc 2.32+）
#   - -L 优先搜索 sysroot 库路径，避免链接到交叉编译器自带的 glibc 2.35 库
# =============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 编译器：gcc-9 对齐盒子（Ubuntu 20.04）。
# 不要用默认的 gcc-11——它的 libstdc++.so 引用 GLIBC_2.32+ 符号，盒子 glibc 2.31 没有。
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc-9)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++-9)
set(CMAKE_AR           aarch64-linux-gnu-ar      CACHE FILEPATH "ar")
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib  CACHE FILEPATH "ranlib")
set(CMAKE_STRIP        aarch64-linux-gnu-strip   CACHE FILEPATH "strip")
set(CMAKE_OBJCOPY      aarch64-linux-gnu-objcopy CACHE FILEPATH "objcopy")

# 独立 sysroot（Ubuntu 20.04 arm64, glibc 2.31，对齐盒子）
set(CMAKE_SYSROOT /opt/rk3568-sysroot)

set(CMAKE_LIBRARY_ARCHITECTURE "aarch64-linux-gnu" CACHE STRING "")

# find_* 搜索路径
set(CMAKE_FIND_ROOT_PATH
    /opt/rk3568-sysroot/usr/aarch64-linux-gnu
    /opt/rk3568-sysroot/usr/include
    /opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu
    /opt/rk3568-sysroot/usr/share
    CACHE INTERNAL "Cross sysroot search paths")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_CMAKE   ONLY)

# pkg-config
set(ENV{PKG_CONFIG_PATH}     "/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:/opt/rk3568-sysroot/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR}   "/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/opt/rk3568-sysroot")

# OpenSSL (Ubuntu 20.04: 1.1.1f)
set(OPENSSL_ROOT_DIR /opt/rk3568-sysroot/usr CACHE PATH "")
set(OPENSSL_INCLUDE_DIR    "/opt/rk3568-sysroot/usr/include"                          CACHE PATH "")
set(OPENSSL_SSL_LIBRARY    "/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/libssl.so"  CACHE FILEPATH "")
set(OPENSSL_CRYPTO_LIBRARY "/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/libcrypto.so" CACHE FILEPATH "")
set(OPENSSL_LIBRARIES      "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}"         CACHE STRING "")
set(OPENSSL_VERSION        "1.1.1f"                                                   CACHE STRING "")

# 关键：-B 把 sysroot 的库路径放在最前面，让 ld 优先找到 sysroot 的 libc.so / libstdc++.so，
# 以及 Scrt1.o / crti.o 这些 crt 启动文件；-L 之后才是同样的路径。
# -B 不能省：--sysroot 只管库搜索路径，不管 crt 启动文件。没有 -B 时 GCC 会先在自己
# multiarch 目录取到宿主 22.04 的 crt（glibc 2.35），再链接 sysroot 的 2.31 libc，
# 启动序列错配 -> .init_array 不执行 -> std::cerr 虚表指针是 0 -> 第一次写标准流就 SIGSEGV。
# 2026-10-16 实测：6 行 hello world（std::cerr << "x"）即可 100% 复现，与项目代码无关。
set(CMAKE_EXE_LINKER_FLAGS
    "-B/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu -L/opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu -L/opt/rk3568-sysroot/lib/aarch64-linux-gnu"
    CACHE STRING "Linker flags (sysroot crt priority)")

# 运行时路径 = 二进制同级目录($ORIGIN)。librknnrt.so / librga.so 由 deploy-wsl.sh 一起
# 部署到盒子，所以 $ORIGIN 在盒子上能解析；写死 .vs 的构建机路径换个机器就失效
# （VS 那份 RUNPATH 就是这么脆的）。
# 不配这一段产物没有 RUNPATH，盒子上直接 ./main 会报
# "librknnrt.so: cannot open shared object file"（2026-10-19 实测）。
# 用 CMake 变量而不是 -Wl,-rpath,\$ORIGIN：Ninja 会把命令行里的 $ORIGIN 当成自己的变量
# 展开成空，readelf 看到的是 RUNPATH: []（2026-10-19 实测）。
set(CMAKE_SKIP_RPATH FALSE CACHE BOOL "Skip RPATH on cross build")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE CACHE BOOL "Use install rpath in build tree")
set(CMAKE_INSTALL_RPATH "$ORIGIN" CACHE STRING "Runtime library search path")
# 只留 $ORIGIN，不要把构建机上的链接库绝对路径也写进产物
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE CACHE BOOL "Append linked lib dirs to rpath")
