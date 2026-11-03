# WSL 交叉编译 (RK3568 / aarch64)

在 WSL Ubuntu-22.04 中交叉编译 washcar，产物直接跑在 RK3568 盒子（Ubuntu 20.04 / glibc 2.31）上。

与 `docker-cross/` 的区别：不需要 Docker，直接用 WSL 原生工具链 + 独立 sysroot，编译更快、内存占用更小。

## 一次性环境准备

在 WSL Ubuntu-22.04 中执行（需要 root）：

```bash
sudo bash wsl-cross/setup-env.sh
```

脚本会自动完成：
1. 配置 apt 多架构源（amd64 用 jammy，arm64 用 focal 对齐盒子 glibc 2.31）
2. 安装 gcc-9 aarch64 交叉编译器 + cmake/ninja/ccache/sshpass
3. 下载 focal arm64 开发库（openssl/ffmpeg/sqlite 等）到 `/opt/rk3568-sysroot`
4. 修复 sysroot 里的 .so 符号链接问题
5. 安装 nlohmann-json / libstdc++-9 / OpenCL 到 sysroot
6. 链接测试验证

> **为什么用 gcc-9 而不是默认的 gcc-11？**
> Ubuntu 22.04 自带的 gcc-11 交叉编译器，其 libstdc++.so 依赖 glibc 2.32+ 符号（`__libc_single_threaded`、`pthread_create@GLIBC_2.34` 等），但盒子是 glibc 2.31，运行会报 `version 'GLIBC_2.34' not found`。gcc-9 的 libstdc++ 兼容 glibc 2.31。

## 日常编译

在 WSL 中，项目根目录下：

```bash
# 增量编译（首次会自动 configure）
bash wsl-cross/build-wsl.sh

# 改了 CMakeLists.txt 后重新 configure
bash wsl-cross/build-wsl.sh configure

# 全量重编
bash wsl-cross/build-wsl.sh rebuild

# 只编某个测试 target（秒级验证）
BUILD_TARGETS="config_manager_test" bash wsl-cross/build-wsl.sh build

# 完整编译输出（排错用）
VERBOSE=1 bash wsl-cross/build-wsl.sh build
```

产物：
- `build-wsl/src/main` — unstripped，29MB，带 debug info，gdb 用
- `build-wsl/main` — stripped，5.5MB，推到盒子跑这个

## 部署到盒子

密码不写进仓库，每次用 `BOX_PASS` 传入：

```bash
# 传输 + ldd 依赖校验
BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh

# 传输 + 校验 + 启动运行 3 秒
BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh run
```

默认盒子 `ztl@192.168.31.248`，目标目录 `~/washcar/deployed`。

可用环境变量覆盖：
```bash
BOX_HOST=user@other-ip BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh
```

## 性能对比

| 方式 | 冷编译 | 增量编译 | 内存占用 |
|------|--------|----------|----------|
| 盒子本机编译 | 3-5 分钟 | 1-2 分钟 | 高（易 OOM） |
| Docker 交叉编译 | 1-2 分钟 | 5-15 秒 | Docker 常驻 1-2GB |
| **WSL 交叉编译** | **~1.5 分钟** | **5-10 秒** | **低（无 Docker 开销）** |

## 文件结构

```
wsl-cross/
├── setup-env.sh                  # 一次性环境初始化（root 执行）
├── toolchain-aarch64-wsl.cmake   # CMake 工具链（gcc-9 + 独立 sysroot）
├── build-wsl.sh                  # 编译脚本
├── deploy-wsl.sh                 # 部署脚本
└── README.md                     # 本文件
```

## 与 docker-cross 的关系

- 两套方案并存，互不干扰。`build-wsl/` 和 `build-cross/` 是独立的构建目录。
- 编译产物完全兼容（都是 aarch64 + glibc 2.31 + gcc-9）。
- 原 Docker 方案可以保留作为备选，也可以停用 Docker Desktop 节省资源。

## 常见问题

**Q: 编译报 `nlohmann/json.hpp: No such file or directory`**
A: sysroot 里没装 nlohmann-json。重新跑 `setup-env.sh`，或手动：
   `cd /tmp && apt-get download nlohmann-json3-dev && sudo dpkg -x nlohmann-json3-dev_*.deb /opt/rk3568-sysroot/`

**Q: 链接报 `undefined reference to pthread_create@GLIBC_2.34`**
A: 用了 gcc-11 的 libstdc++。确认 toolchain 文件里是 `aarch64-linux-gnu-gcc-9`，且 sysroot 里有 `libstdc++.so`（20.04 版本 6.0.28）。

**Q: 链接器卡死（无限循环打开 libpthread.so.0）**
A: sysroot 里的 `libpthread.so` 是符号链接指向 `.so.0`，ld 在 --sysroot 下处理符号链接会死循环。setup-env.sh 会把这些 .so 替换为实际 ELF 文件副本。手动修复：
   `sudo cp /opt/rk3568-sysroot/lib/aarch64-linux-gnu/libpthread-2.31.so /opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/libpthread.so`

**Q: 盒子上跑报 `libxxx.so: cannot open shared object file`**
A: 盒子上缺那个库。在盒子上 `sudo apt install libxxx-dev:arm64`，或确认盒子系统版本是 Ubuntu 20.04。
