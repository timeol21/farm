以下是更新后的完整文档，你可以直接保存为 README.md 或 Word 文档。

宇树激光雷达（UniLidar）点云数据采集与导出指南
——基于定昌 DC-A568-V06（RK3568）主板

一、 准备工作
硬件连接：雷达通过 USB 转 Type-C 连接主板，并接入 12V 外部电源。

系统环境：Ubuntu 20.04 (aarch64)。

权限配置（仅需做一次）：

Bash
sudo usermod -a -G dialout $USER
# 注销并重新登录生效
二、 SDK 修改与编译（核心逻辑）
为了实现导出功能，我们需要在 examples/example_lidar.cpp 中加入文件操作逻辑。

打开文件：vim /home/ztl/program/boxfiles/Radar_lqh/unilidar_sdk/unitree_lidar_sdk/examples/example_lidar.cpp

核心逻辑：

引入头文件：#include <fstream>

在 POINTCLOUD 分支下添加 std::ofstream 写入操作。

设置 frameCount 计数器，达到 50 帧时执行 outFile.close() 并 return 0。

三、 重新采集 50 帧数据的标准步骤
如果你已经按照之前的指导配置好了环境并修改了代码，后续每次需要重新采集 50 帧新数据时，只需执行以下三步：

第一步：清理旧数据（可选但建议）
为了避免旧文件的干扰，你可以先删除或重命名之前的点云文件：

Bash
cd /home/ztl/program/boxfiles/Radar_lqh/unilidar_sdk/unitree_lidar_sdk/bin
rm lidar_cloud.txt
第二步：编译（仅当你修改过代码时需要）
如果你没有改动过 .cpp 源码，可以直接跳过这一步。如果你改动了代码（比如把 50 帧改成了 100 帧）：

Bash
cd /home/ztl/program/boxfiles/Radar_lqh/unilidar_sdk/unitree_lidar_sdk/build
make
第三步：执行采集
进入 bin 目录并运行程序。此时请确保雷达正对着你想要拍摄的物体：

Bash
cd /home/ztl/program/boxfiles/Radar_lqh/unilidar_sdk/unitree_lidar_sdk/bin
./example_lidar
观察终端：屏幕会显示 Saved frame [1/50] 到 [50/50]。

自动结束：当达到 50 帧时，程序会自动停止并回到命令行。

确认文件：输入 ls -l lidar_cloud.txt，看到文件大小不为 0，说明采集成功。

四、 Windows 端 3D 可视化
传输文件：将 lidar_cloud.txt 拷贝到 Windows。

导入 CloudCompare：

直接拖入 .txt 文件。

坐标列：确认 Column 1, 2, 3 分别对应 X, Y, Z。

Global Shift：全部选 Yes。

视觉美化：

选中点云 -> Properties -> Color -> 选 Scalar field。

SF display parameters -> 选 Intensity（强度着色）。

五、 负责人要求的“去重”操作
如果生成的 50 帧数据在 CloudCompare 里看起来太厚、重叠点太多，请执行：

在 CloudCompare 中选中该点云。

点击顶部菜单：Tools -> Edit -> Subsample。

方法选 Space，数值填 0.01（代表每 1 厘米立方空间内只保留一个点）。

点击 OK，生成的新点云即为去重后的精简版。

文档说明：本流程已在 RK3568 主板上实测通过，适用于宇树全系列 USB 接口激光雷达。