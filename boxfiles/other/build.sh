#!/bin/bash

# chmod +x build.sh
# 如果用户传了数字，就用用户的，否则自动获取 CPU 核心数
if [ -n "$1" ] && [[ "$1" =~ ^[0-9]+$ ]]; then
    CORE_NUM="$1"
else
    CORE_NUM=$(nproc)
fi

echo "🚀 使用编译核心数：$CORE_NUM"

mkdir -p build
cd build
cmake ..
make -j"${CORE_NUM}"

echo -e "\n✅ 编译完成！运行程序..."
./plc