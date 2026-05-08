# 可明文修改输入输出文件名

# python3 calc_avg_z.py

# ===================== 可修改文件名 =====================
INPUT_TXT = "scans.txt"     # 输入文件
OUTPUT_TXT = "result.txt"   # 输出文件
# ========================================================

import os
from collections import defaultdict

# 存储：key=(x,y)，value=[z值列表]
group = defaultdict(list)
# 存储：key=(x,y)，value=原始行后面4~8列内容（原样保存）
original_cols = {}
raw_point_count = 0

# ===================== 读取：只提取信息，不修改原文件 =====================
with open(INPUT_TXT, "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue

        raw_point_count += 1
        parts = line.split()

        x = float(parts[0])
        y = float(parts[1])
        z = float(parts[2])

        # 后面所有列 原样保存
        rest = parts[3:]

        # 分组
        group[(x, y)].append(z)
        # 后面的列直接保存，保证输出时完全一样
        original_cols[(x, y)] = rest

# ===================== 写入：只改 z，其余完全原样输出 =====================
point_count = 0
with open(OUTPUT_TXT, "w", encoding="utf-8") as f:
    for (x, y), z_list in group.items():
        # 只计算平均 z
        avg_z = sum(z_list) / len(z_list)
        # 后面列 100% 原样不变
        rest = original_cols[(x, y)]
        rest_str = " ".join(rest)

        # 写入：x y 不变，z 取平均，后面原样
        f.write(f"{x:.6f} {y:.6f} {avg_z:.6f} {rest_str}\n")
        point_count += 1

# ===================== 打印信息 =====================
input_size_mb = os.path.getsize(INPUT_TXT) / (1024 * 1024)
output_size_mb = os.path.getsize(OUTPUT_TXT) / (1024 * 1024)

print("=" * 50)
print(f"处理前文件：{INPUT_TXT}")
print(f"  点数：{raw_point_count} 个")
print(f"  大小：{input_size_mb:.2f} MB")
print("-" * 50)
print(f"处理后文件：{OUTPUT_TXT}")
print(f"  点数：{point_count} 个")
print(f"  大小：{output_size_mb:.2f} MB")
print("=" * 50)