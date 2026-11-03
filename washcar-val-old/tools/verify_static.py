#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""静态一致性检查（无交叉编译器时的替代验证）。

检查三件事：
 1. src/CMakeLists.txt 里 SRC_FILES 列出的每个 .cpp/.c 都真实存在；
 2. 这些编译单元里的 #include "..." 都能在头文件搜索路径下解析到；
 3. 已删除模块的标识符 / 头文件不再被编译单元引用（只查非注释行）。

用法： python tools/verify_static.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INCLUDE_DIRS = [
    "include",
    "src",
    "lib/rknn/utils",
    "lib/rknn/3rdparty/rknpu2/include",
    "include/common/ai/yolov8",
    "lib/rknn/3rdparty/librga/include",
    "lib/rknn/3rdparty/stb_image",
    "lib/rknn/3rdparty/jpeg_turbo/include",
]

# 已删除模块的标识符（编译单元里非注释行不应再出现）
FORBIDDEN = [
    "GPIODeviceInstanceSet", "DoorLockStatus", "DoorLockOperation",
    "GPIODeviceSimpleInfo", "TempHumidSensorStatus", "TempHumidSensor",
    "SerialDirectDeviceInstanceSet", "SerialDirectDevice",
    "RadarPointCloud", "RadarInfo", "CarControl", "BoxInstance",
    "BoxConfigResult", "BoxDeviceParam", "BoxDeviceRealTimeData",
]


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def strip_comments(src):
    """去掉 // 与 /* */ 注释，保留字符串字面量里的内容（够用即可）。"""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            out.append(c)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == "\\":
                    if i + 1 < n:
                        out.append(src[i + 1])
                        i += 2
                        continue
                elif src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_src_files():
    cm = read(os.path.join(ROOT, "src", "CMakeLists.txt"))
    m = re.search(r"set\(SRC_FILES(.*?)\n\)", cm, re.S)
    if not m:
        sys.exit("无法在 src/CMakeLists.txt 中找到 SRC_FILES")
    files = []
    for line in m.group(1).splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("${CMAKE_SOURCE_DIR}"):
            path = line.replace("${CMAKE_SOURCE_DIR}", ROOT).replace("/", os.sep)
        else:
            path = os.path.join(ROOT, "src", line.replace("/", os.sep))
        files.append(os.path.normpath(path))
    return files


def find_header(name, from_file=None):
    """返回解析到的头文件绝对路径，找不到返回 None。"""
    if from_file:
        cand = os.path.join(os.path.dirname(from_file), name.replace("/", os.sep))
        if os.path.isfile(cand):
            return os.path.normpath(cand)
    for d in INCLUDE_DIRS:
        cand = os.path.join(ROOT, d, name.replace("/", os.sep))
        if os.path.isfile(cand):
            return os.path.normpath(cand)
    return None


def resolve_include(name, from_file=None):
    return find_header(name, from_file) is not None


# 外部/系统头文件（来自交叉编译 sysroot 或系统 include，不属于本项目源码）
EXTERNAL_PREFIXES = (
    "nlohmann/", "openssl/", "libavcodec/", "libavformat/", "libavutil/",
    "libswscale/", "sys/", "linux/", "netinet/", "arpa/", "rknn", "rga/",
    "stb_", "turbojpeg", "jpeglib", "unistd.h", "pthread.h", "termios.h",
    "fcntl.h", "signal.h", "dirent.h", "syslog.h", "semaphore.h",
)


def is_external(name):
    return name.startswith(EXTERNAL_PREFIXES)


def main():
    errors = []
    src_files = parse_src_files()
    print("SRC_FILES 条目数: %d" % len(src_files))

    for f in src_files:
        if not os.path.isfile(f):
            errors.append("SRC_FILES 指向不存在的文件: %s" % os.path.relpath(f, ROOT))

    # sqlite3.c 是厂商 amalgamation，内部 #include 全在条件编译分支里，跳过
    existing = [f for f in src_files if os.path.isfile(f) and not f.endswith("sqlite3.c")]
    for f in existing:
        src = strip_comments(read(f))
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', src):
            if resolve_include(inc, f):
                continue
            if is_external(inc):
                continue
            errors.append("%s: 头文件无法解析 -> %s" % (os.path.relpath(f, ROOT), inc))

        rel = os.path.relpath(f, ROOT)
        for token in FORBIDDEN:
            if re.search(r"\b%s\b" % re.escape(token), src):
                errors.append("%s: 仍引用已删除符号 %s" % (rel, token))

    # 递归检查头文件：被编译单元直接/间接包含的头文件，其自身 include 也必须可解析。
    seen = set()
    queue = []
    for f in existing:
        body = strip_comments(read(f))
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', body):
            r = find_header(inc, f)
            if r:
                queue.append(r)

    while queue:
        cur = queue.pop()
        key = os.path.normpath(cur)
        if key in seen or not key.endswith((".h", ".hpp", ".hh")):
            continue
        seen.add(key)
        body = strip_comments(read(key))
        for inc in re.findall(r'#\s*include\s*"([^"]+)"', body):
            resolved = find_header(inc, key)
            if resolved:
                queue.append(resolved)
            elif not is_external(inc):
                errors.append("%s（被包含的头）: 无法解析 -> %s"
                              % (os.path.relpath(key, ROOT), inc))
    print("递归检查头文件数: %d" % len(seen))

    if errors:
        print("\n失败 %d 项：" % len(errors))
        for e in errors:
            print("  - " + e)
        return 1

    print("通过：SRC_FILES 全部存在，头文件全部可解析，无已删除符号引用。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
