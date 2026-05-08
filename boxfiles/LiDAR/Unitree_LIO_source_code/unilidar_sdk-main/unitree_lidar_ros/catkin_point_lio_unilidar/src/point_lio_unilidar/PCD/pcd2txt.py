# ===================== 在这里改你的 PCD 路径 =====================
pcd_file = "scans.pcd"  # 可以写文件名 或 完整路径
# =================================================================

# python3 pcd2txt.py 

import struct
import os

def read_pcd_full(pcd_path, txt_path):
    try:
        with open(pcd_path, 'rb') as f:
            header = []
            while True:
                line = f.readline()
                if not line:
                    break
                if line.startswith(b'#'):
                    continue
                header.append(line)
                if b'DATA' in line:
                    break

            # 解析头部
            fields = []
            sizes = []
            types = []
            counts = []
            width = 1
            height = 1
            points_num = 0
            data_type = ""

            for line in header:
                s = line.decode('utf-8', 'ignore').strip()
                if s.startswith('FIELDS'):
                    fields = s.split()[1:]
                if s.startswith('SIZE'):
                    sizes = list(map(int, s.split()[1:]))
                if s.startswith('TYPE'):
                    types = s.split()[1:]
                if s.startswith('COUNT'):
                    counts = list(map(int, s.split()[1:]))
                if s.startswith('WIDTH'):
                    width = int(s.split()[1])
                if s.startswith('HEIGHT'):
                    height = int(s.split()[1])
                if s.startswith('POINTS'):
                    points_num = int(s.split()[1])
                if s.startswith('DATA'):
                    data_type = s.split()[1]

            print(f"📌 检测到字段：{fields}")
            print(f"📌 数据格式：{data_type}")
            print(f"📌 总点数：{points_num}")

            # 计算每个点总长度
            point_size = sum(sizes)
            points_out = []

            # ======================================
            # 二进制格式（最常见）
            # ======================================
            if data_type == "binary":
                for i in range(points_num):
                    data = f.read(point_size)
                    if len(data) < point_size:
                        break

                    idx = 0
                    row = []
                    for j in range(len(fields)):
                        s = sizes[j]
                        t = types[j]

                        if t == 'F' and s == 4:
                            val = struct.unpack('f', data[idx:idx+4])[0]
                            row.append(f"{val:.6f}")
                        elif t == 'I' and s == 4:
                            val = struct.unpack('i', data[idx:idx+4])[0]
                            row.append(f"{val}")
                        elif t == 'U' and s == 4:
                            val = struct.unpack('I', data[idx:idx+4])[0]
                            row.append(f"{val}")
                        elif t == 'F' and s == 8:
                            val = struct.unpack('d', data[idx:idx+8])[0]
                            row.append(f"{val:.6f}")
                        else:
                            row.append("unknown")

                        idx += s

                    points_out.append(" ".join(row))

            # ======================================
            # ASCII 格式
            # ======================================
            else:
                for i in range(points_num):
                    line = f.readline().decode('utf-8', 'ignore').strip()
                    if line:
                        points_out.append(line)

            # 写入 TXT
            with open(txt_path, 'w', encoding='utf-8') as f_out:
                f_out.write('\n'.join(points_out))

            print("✅ 导出完成！所有字段全部保存！")
            print(f"📄 输出文件：{txt_path}")
            print(f"📊 每行包含：{' '.join(fields)}")

    except Exception as e:
        print(f"❌ 错误：{e}")

if __name__ == "__main__":
    txt_out = os.path.splitext(pcd_file)[0] + ".txt"
    read_pcd_full(pcd_file, txt_out)


# # ===================== 在这里改你的 PCD 路径 =====================    
# pcd_file = "scans.pcd"  # 可以写文件名 或 完整路径
# 测试有效，只有x,y,z的坐标
# # =================================================================

# # python3 pcd2txt.py

# import struct
# import os

# def read_pcd_write_txt(pcd_path, txt_path):
#     try:
#         with open(pcd_path, 'rb') as f:
#             lines = []
#             header = []
#             while True:
#                 line = f.readline()
#                 if not line:
#                     break
#                 if line.startswith(b'#'):
#                     continue
#                 header.append(line)
#                 if b'DATA' in line:
#                     break

#             # 读取头部信息
#             width = 1
#             height = 1
#             points_size = 0
#             data_type = ""
#             fields = []
#             sizes = []

#             for line in header:
#                 line_str = line.decode('utf-8', 'ignore').strip()
#                 if line_str.startswith('WIDTH'):
#                     width = int(line_str.split()[1])
#                 if line_str.startswith('HEIGHT'):
#                     height = int(line_str.split()[1])
#                 if line_str.startswith('POINTS'):
#                     points_size = int(line_str.split()[1])
#                 if line_str.startswith('DATA'):
#                     data_type = line_str.split()[1]
#                 if line_str.startswith('FIELDS'):
#                     fields = line_str.split()[1:]
#                 if line_str.startswith('SIZE'):
#                     sizes = list(map(int, line_str.split()[1:]))

#             # 计算点步长
#             step = sum(sizes)
#             points = []

#             # 读取二进制数据
#             if data_type == 'binary':
#                 for _ in range(points_size):
#                     data = f.read(step)
#                     if len(data) < step:
#                         break
#                     x = struct.unpack('f', data[0:4])[0]
#                     y = struct.unpack('f', data[4:8])[0]
#                     z = struct.unpack('f', data[8:12])[0]
#                     points.append(f"{x:.6f} {y:.6f} {z:.6f}")

#             # 读取ASCII数据
#             else:
#                 for _ in range(points_size):
#                     line = f.readline().decode('utf-8', 'ignore').strip()
#                     parts = line.split()
#                     if len(parts) >= 3:
#                         x, y, z = parts[:3]
#                         points.append(f"{x} {y} {z}")

#         # 写入txt
#         with open(txt_path, 'w', encoding='utf-8') as f:
#             f.write('\n'.join(points))

#         print("✅ 转换成功！")
#         print(f"📂 输入：{pcd_path}")
#         print(f"📄 输出：{txt_path}")
#         print(f"📊 有效点数：{len(points)}")

#     except Exception as e:
#         print(f"❌ 错误：{e}")

# if __name__ == "__main__":
#     out_txt = os.path.splitext(pcd_file)[0] + ".txt"
#     read_pcd_write_txt(pcd_file, out_txt)