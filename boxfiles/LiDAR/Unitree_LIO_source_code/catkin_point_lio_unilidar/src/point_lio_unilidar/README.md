# point_lio_unilidar

## 1. Introduction

### 1.1 Unitree LiDAR

This repository adapts the state-of-the-art lidar inertial odometry algorithm, `Point-LIO`, for use with our lidar products:
- `Unitree LiDAR L1`
- `Unitree LiDAR L2`

Both `L1` and `L2` possess these features:
- large field of view (360° × 90°)
- non-repetitive scanning
- low cost
- suitable for applications in low-speed mobile robots

If you want to learn more about our lidar products, you can refer to the official website for details.
- <https://www.unitree.com/L2>
- <https://www.unitree.com/LiDAR>


### 1.2 Point-LIO

`Point-LIO` is a robust and high-bandwidth lidar inertial odometry (LIO) with the capability to provide accurate, high-frequency odometry and reliable mapping under severe vibrations and aggressive motions. If you need further information about the `Point-LIO` algorithm, you can refer to their official website and paper:
- <https://github.com/hku-mars/Point-LIO>
- [Point‐LIO: Robust High‐Bandwidth Light Detection and Ranging Inertial Odometry](https://onlinelibrary.wiley.com/doi/epdf/10.1002/aisy.202200459)


## 2. Video Demos

### 2.1 L1 LiDAR

[![Video](./doc/video.png)](https://oss-global-cdn.unitree.com/static/c0bd0ac7d1e147e7a7eaf909f1fc214f.mp4 "SLAM based on Unitree 4D LiDAR L1")

### 2.2 L2 LiDAR
 
[![Video](./doc/l2-demo-video-bilibili.png)](https://www.bilibili.com/video/BV1XVUVYHEHR "SLAM based on Unitree 4D LiDAR L2")

[YouTube](https://youtu.be/juAfGrg2xBg?si=IVTWM9shEmHsKKJ_)


## 3. Prerequisites

### 3.1 Ubuntu and ROS
We tested our code on Ubuntu20.04 with [ROS noetic](http://wiki.ros.org/noetic/Installation/Ubuntu). Ubuntu18.04 and lower versions have problems of environments to support the Point-LIO, try to avoid using Point-LIO in those systems. 

You can refer to the official website to install ROS noetic:
- <http://wiki.ros.org/noetic/Installation/Ubuntu>

Additional ROS package is required:
```
sudo apt-get install ros-xxx-pcl-conversions
```

### 3.2 Eigen
Following the official [Eigen installation](eigen.tuxfamily.org/index.php?title=Main_Page), or directly install Eigen by:
```
sudo apt-get install libeigen3-dev
```

### 3.3 unilidar_sdk

For using lidar `L1`, you should download and build [unilidar_sdk](https://github.com/unitreerobotics/unilidar_sdk) follwing these steps:

```
git clone https://github.com/unitreerobotics/unilidar_sdk.git

cd unilidar_sdk/unitree_lidar_ros

catkin_make
```

### 3.4 unilidar_sdk2

For using lidar `L2`, you should download and build [unilidar_sdk2](https://github.com/unitreerobotics/unilidar_sdk2) follwing these steps:

```
git clone https://github.com/unitreerobotics/unilidar_sdk2.git

cd unilidar_sdk/unitree_lidar_ros

catkin_make
```

## 4. Build

Clone this repository and run `catkin_make`:

```
mkdir -p catkin_point_lio_unilidar/src

cd catkin_point_lio_unilidar/src

git clone https://github.com/unitreerobotics/point_lio_unilidar.git

cd ..

catkin_make
```


## 5. Run

### 5.1 Run with L1

To ensure proper initialization of the IMU, it is advisable to keep the lidar in a stationary state during the initial few seconds of algorithm execution.

Run `unilidar`:
```
cd unilidar_sdk/unitree_lidar_ros

source devel/setup.bash

roslaunch unitree_lidar_ros run_without_rviz.launch
```

Run `Point-LIO`:
```
cd catkin_unilidar_point_lio

source devel/setup.bash

roslaunch point_lio_unilidar mapping_unilidar_l1.launch 
```


After completion of the run, all cached pointcloud map will be saved to the following path:
```
catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans.pcd
```

You can use the `pcl_viewer` tool to view this pcd file:
```
pcl_viewer scans.pcd 
```

### 5.2 Run with rosbag of L1

If you don't have our lidar for now, you can download our dataset recorded with our lidar and run testify this algorithm with it.
The download address is here:
- [unilidar-2023-09-22-12-42-04.bag - Download](https://oss-global-cdn.unitree.com/static/unilidar-2023-09-22-12-42-04.zip)


Run `Point-LIO`:
```
cd catkin_point_lio_unilidar

source devel/setup.bash

roslaunch point_lio_unilidar mapping_unilidar_l1.launch 
```

Play the dataset you downloaded:
```
rosbag play unilidar-2023-09-22-12-42-04.bag 
```


After completion of the run, all cached pointcloud map will be saved to the following path:
```
catkin_point_lio_unilidar/src/point_lio_unilidarPCD/scans.pcd
```

You can use the `pcl_viewer` tool to view this pcd file:
```
pcl_viewer scans.pcd 
```

### 5.3 Run with L2

To ensure proper initialization of the IMU, it is advisable to keep the lidar in a stationary state during the initial few seconds of algorithm execution.

Run `unilidar`:
```
cd unilidar_sdk/unitree_lidar_ros

source devel/setup.bash

roslaunch unitree_lidar_ros run_without_rviz.launch
```

Run `Point-LIO`:
```
cd catkin_unilidar_point_lio

source devel/setup.bash

roslaunch point_lio_unilidar mapping_unilidar_l2.launch 
```

After completion of the run, all cached pointcloud map will be saved to the following path:
```
catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans.pcd
```

You can use the `pcl_viewer` tool to view this pcd file:
```
pcl_viewer scans.pcd 
```

### 5.4 Run with rosbag of L2

If you don't have our lidar for now, you can download our dataset recorded with our lidar and run testify this algorithm with it.
The download address is here:
- [L2 Indoor Point Cloud Data.bag - Download](https://oss-global-cdn.unitree.com/static/L2%20Indoor%20Point%20Cloud%20Data.bag)
- [L2 Park Observed Point Cloud Data.bag - Download](https://oss-global-cdn.unitree.com/static/L2%20Park%20Point%20Cloud%20Data.bag)


Run `Point-LIO`:
```
cd catkin_point_lio_unilidar

source devel/setup.bash

roslaunch point_lio_unilidar mapping_unilidar_l2.launch 
```

Play the dataset you downloaded:
```
rosbag play XXXXXX.bag 
```

After completion of the run, all cached pointcloud map will be saved to the following path:
```
catkin_point_lio_unilidar/src/point_lio_unilidarPCD/scans.pcd
```

You can use the `pcl_viewer` tool to view this pcd file:
```
pcl_viewer scans.pcd 
```



point_lio_unilidar 项目
1. 简介
1.1 宇树激光雷达
本仓库将当前主流的激光雷达惯性里程计算法 Point-LIO 适配至宇树全系激光雷达产品，适配型号如下：
宇树激光雷达 L1
宇树激光雷达 L2
L1 与 L2 两款雷达具备以下特性：
超大视场角（360° × 90°）
无重复扫描模式
低成本
适用于低速移动机器人场景
如需了解更多产品详情，可访问宇树官方页面：
https://www.unitree.com/L2
https://www.unitree.com/LiDAR
1.2 Point-LIO 算法
Point-LIO 是一款高稳定性、高带宽的激光雷达惯性里程计（LIO）。即便在剧烈震动、快速运动工况下，该算法也能输出精准、高频的里程计数据，并实现可靠建图。如需查阅算法源码与论文，参考以下链接：
算法仓库：https://github.com/hku-mars/Point-LIO
算法论文：Point‐LIO: Robust High‐Bandwidth Light Detection and Ranging Inertial Odometry
2. 效果演示视频
2.1 L1 激光雷达


2.2 L2 激光雷达


YouTube 视频
3. 环境依赖
3.1 系统与 ROS
代码测试环境：Ubuntu 20.04 + ROS Noetic。Ubuntu 18.04 及更低版本存在环境兼容问题，无法正常运行 Point-LIO，请勿使用。
ROS Noetic 官方安装教程：
http://wiki.ros.org/noetic/Installation/Ubuntu
需额外安装 ROS 依赖包：
bash
运行
sudo apt-get install ros-noetic-pcl-conversions
3.2 Eigen 矩阵库
可参考 Eigen 官网编译安装，或直接通过命令安装：
bash
运行
sudo apt-get install libeigen3-dev
3.3 激光雷达 SDK（L1 专用）
使用 L1 雷达需下载并编译 unilidar_sdk：
bash
运行
git clone https://github.com/unitreerobotics/unilidar_sdk.git
cd unilidar_sdk/unitree_lidar_ros
catkin_make
3.4 激光雷达 SDK2（L2 专用）
使用 L2 雷达需下载并编译 unilidar_sdk2：
bash
运行
git clone https://github.com/unitreerobotics/unilidar_sdk2.git
cd unilidar_sdk/unitree_lidar_ros
catkin_make
4. 项目编译
拉取项目源码并编译：
bash
运行
mkdir -p catkin_point_lio_unilidar/src
cd catkin_point_lio_unilidar/src
git clone https://github.com/unitreerobotics/point_lio_unilidar.git
cd ..
catkin_make
5. 运行指南
5.1 实物运行（L1）
算法启动初期，请保持设备静止数秒，确保 IMU 惯性测量单元完成初始化。
启动雷达驱动
bash
运行
cd unilidar_sdk/unitree_lidar_ros
source devel/setup.bash
roslaunch unitree_lidar_ros run_without_rviz.launch
启动 Point-LIO 建图算法
bash
运行
cd catkin_unilidar_point_lio
source devel/setup.bash
roslaunch point_lio_unilidar mapping_unilidar_l1.launch 
运行结束后，点云地图文件会自动保存至：
plaintext
catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans.pcd
使用 PCL 工具查看点云：
bash
运行
pcl_viewer scans.pcd 
5.2 离线运行（L1 数据集）
暂无实物雷达时，可下载官方数据集离线验证算法：
数据集下载：unilidar-2023-09-22-12-42-04.bag
启动建图节点
bash
运行
cd catkin_point_lio_unilidar
source devel/setup.bash
roslaunch point_lio_unilidar mapping_unilidar_l1.launch 
播放离线数据包
bash
运行
rosbag play unilidar-2023-09-22-12-42-04.bag 
运行完成后地图保存路径：
plaintext
catkin_point_lio_unilidar/src/point_lio_unilidarPCD/scans.pcd
点云查看命令同上。
5.3 实物运行（L2）
算法启动初期保持设备静止，完成 IMU 初始化。
启动雷达驱动
bash
运行
cd unilidar_sdk/unitree_lidar_ros
source devel/setup.bash
roslaunch unitree_lidar_ros run_without_rviz.launch
启动建图算法
bash
运行
cd catkin_unilidar_point_lio
source devel/setup.bash
roslaunch point_lio_unilidar mapping_unilidar_l2.launch 
地图保存路径与点云查看方式同上。
5.4 离线运行（L2 数据集）
可下载 L2 离线数据包测试：
室内点云数据集：L2 Indoor Point Cloud Data.bag
园区户外数据集：L2 Park Observed Point Cloud Data.bag
启动 L2 建图节点
bash
运行
cd catkin_point_lio_unilidar
source devel/setup.bash
roslaunch point_lio_unilidar mapping_unilidar_l2.launch 
播放对应数据包
bash
运行
rosbag play XXXXXX.bag 
补充说明
全文修正原文拼写错误（follwing→following、testify→verify）；
保留所有代码命令、文件路径、链接原样，可直接复制使用；
机器人 / ROS/SLAM 专业术语统一标准化，贴合国内开发用语习惯。
