# Unitree Lidar SDK

## Introduction
This package is a cmake package, which is specially used for running `Unitree LiDAR L1`.

The functions that this package can provide includes:
- Parse the raw data transmitted from the lidar hardware, and convert it into pointcloud and IMU data
- Get the pointcloud data
- Get the IMU data

The output pointcloud defaultly uses a self-defined data type so that this SDK doesn't rely too much on external dependencies. In other case,
- if you are used to use [Point Cloud Library](https://pointclouds.org/), you can use the header `unitree_lidar_sdk_pcl.h` to transform our pointcloud to PCL format;
- if you wish to directly use a [ROS](https://www.ros.org/) package, you are also able to utilize our ROS pacakge for this lidar.

## Dependency
We have verified that this package can successfully run under this environment:
- `Ubuntu 20.04` 

This SDK hardly depends on any external dependencies.
But if you want to use PCL cloud format, you need to install one. 

## Configure

Connect your lidar to your computer with a USB cable, then confirm your serial port name for lidar:
```
$ ls /dev/ttyUSB*
/dev/ttyUSB0
```

The default serial port name is `/dev/ttyUSB0`.
If it is not the default one, you need to modify the configuration parameter in `example_lidar.cpp`.

## Build

You can build this program as a cmake project:
```
cd unitree_lidar_sdk

mkdir build

cd build

cmake .. && make -j2
```

## Run
Directly run the example:
```
../bin/example_lidar
```

The output is like this:
```
$ ../bin/example_lidar 
lidar firmware version = 0.3.2+230511
lidar sdk version = 1.0.3

Dirty Percentage = 5.145833 %
Dirty Percentage = 4.166667 %
Dirty Percentage = 4.166667 %

Turn on all the LED lights ...
Turn off all the LED lights ...
Set LED mode to: FORWARD_SLOW ...
Set LED mode to: REVERSE_SLOW ...
Set LED mode to: SIXSTAGE_BREATHING ...

Set Lidar working mode to: NORMAL_MODE ... 

An IMU msg is parsed!
	stamp = 1683874160.559222, id = 729
	quaternion (x, y, z, w) = [0.0131, -0.0091, 0.6888, -0.7225]

An IMU msg is parsed!
	stamp = 1683874160.564979, id = 121
	quaternion (x, y, z, w) = [0.0102, -0.0093, 0.7099, -0.7018]

An IMU msg is parsed!
	stamp = 1683874160.568425, id = 122
	quaternion (x, y, z, w) = [0.0118, -0.0096, 0.7099, -0.7018]

An IMU msg is parsed!
	stamp = 1683874160.573472, id = 123
	quaternion (x, y, z, w) = [0.0126, -0.0093, 0.7098, -0.7018]

An IMU msg is parsed!
	stamp = 1683874160.577348, id = 124
	quaternion (x, y, z, w) = [0.0128, -0.0093, 0.7099, -0.7018]

A Cloud msg is parsed! 
	stamp = 1683874145.535888, id = 1
	cloud size  = 278
	first 10 points (x,y,z,intensity,time,ring) = 
	  (-0.029885, -0.136897, 0.000448, 88.000000, 0.000000, 0)
	  (-0.035384, -0.171399, 0.005140, 91.000000, 0.000023, 0)
	  (-0.043000, -0.219542, 0.012437, 127.000000, 0.000046, 0)
	  (-0.054879, -0.294965, 0.024572, 132.000000, 0.000069, 0)
	  (-0.055288, -0.301204, 0.033170, 106.000000, 0.000093, 0)
	  (-0.054542, -0.300348, 0.041173, 101.000000, 0.000116, 0)
	  (-0.053764, -0.299285, 0.049148, 99.000000, 0.000139, 0)
	  (-0.056123, -0.318406, 0.060981, 92.000000, 0.000162, 0)
	  (-0.055230, -0.316827, 0.069422, 91.000000, 0.000185, 0)
	  (-0.051257, -0.294854, 0.072849, 128.000000, 0.000208, 0)
	  ...
```

Here, we print the first 10 points of the pointcloud message and the quaternion of the IMU message.

**Notice**:
- In Ubuntu, accessing a serial port device requires the appropriate permissions. If your C++ program does not have sufficient permissions to access the serial port device, you will get a **"Permission denied"** error.
- To solve this error, you can use the following command to add the current user to the dialout group:
```
sudo usermod -a -G dialout $USER
```
- After adding the user to the dialout group, you need to log out and log back in for the changes to take effect.


## How To Parse Point Cloud From MavLink Messages

If you want to parse point cloud from MavLink Messages which are acquired from serial directly, you can refer to this document.
- [HowToParsePointCloudAndIMUDataFromMavLinkMessages.md](./doc/HowToParsePointCloudAndIMUDataFromMavLinkMessages.md)

## Version History

### v1.0.0 (2023.05.04)
- Support firmware version: 0.3.1

### v1.0.1 (2023.05.05)
- Support firmware version: 0.3.1
- Add support of setting lidar working mode, e.g. `NORMAL_MODE` and `STANDBY_MODE`
- Add support of LED lights

### v1.0.2 (2023.05.11)
- Support firmware version: 0.3.2

### v1.0.3 (2023.05.12)
- Support firmware version: 0.3.2
- Add support of getting the percentage of removed dirty points

### v1.0.4 (2023.05.30)
- Support firmware version: 1.0.1

### v1.0.5 (2023.06.05)
- Support firmware version: 1.0.1
- Update default `rotate_yaw_bias` to calibrated value `-38.5` degree.
- Update `README.md` with notice to solve the "Permission denied" error while opening serial port.

### v1.0.6 (2023.06.19)
- Support firmware version: 1.0.1
- Modify the `initialize()` function to check whether the specified serial port exist. If the serial port name does not exist, initialization fails and `return -1` rather than throw out an error unexpectedly。
- Add z bias to lidar basis plane

### v1.0.7 (2023.06.21)
- Support firmware version: 1.0.1
- Add coordinate system definition in readme
- Modified readme of `unitree_lidar_ros` and `unitree_lidar_ros2`

### v1.0.8 (2023.06.28)
- Support firmware version: 1.0.1
- Solve the dependency problem --- `cannot find -llz4`
- Add mavlink headers for optional use

### v1.0.9 (2023.06.30)
- Support firmware version: 1.0.1
- Delete the date postfix of firmware version
- Improve support of setting lidar working mode, which can switch between `NORMAL_MODE` and `STANDBY_MODE`

### v1.0.10 (2023.07.17)
- Support firmware version: 1.0.1
- Add a UDP publisher written in C++, which can publish lidar data to a specified IP address and port
- Add a UDP subscriber example written in C++, which is able to subscibe scan and imu data mentioned above
- Add a UDP subscriber example written in Python, which is able to subscibe scan and imu data mentioned above
- Add a compiled install package for Windows user, though which you can acquire lidar data in windows.

### v1.0.11 (2023.07.27)
- Add a instruction document for how to parse point cloud and imu data from mavlink messages directly.
- The document is `HowToParsePointCloudAndIMUDataFromMavLinkMessages.md`

### v1.0.12 (2023.09.20)
- Add support of UDP interface, which can parse original bytes from a specified UDP port and send commands to lidar ip and port.
- Update `unitree_lidar_ros`

### v1.0.13 (2023.10.09)
- Modify the serial port reading method in `unitree_lidar_sdk`. It will block when you call `runParse()` function, waiting for the serial port to have bytes and then reading the data once.
- Support Serial-to-UDP adapter board

### v1.0.14 (2023.10.30)
- Repair occasional segmentation fault when using serial-to-udp board.

### v1.0.15 (2024.01.04)
- Modify default lidar and local ip address in `example_lidar_udp.cpp`

### v1.0.16 (2024.02.18)
- Add a function to close UDP connection
- Set the timeout to 1 second for `initializeUDP()`



宇树激光雷达 SDK（Unitree Lidar SDK）

简介

本软件包为 CMake 工程包，专门用于运行 `Unitree LiDAR L1`。

本 SDK 可提供的功能包括：

- 解析激光雷达硬件发送的原始数据，并转换为点云与 IMU 数据

- 获取点云数据

- 获取 IMU 数据

输出的点云默认使用自定义数据类型，因此本 SDK 对外界依赖极少。此外：

- 若你习惯使用 Point Cloud Library，可通过头文件 `unitree_lidar_sdk_pcl.h` 将我们的点云转换为 PCL 格式；

- 若你希望直接使用 ROS 功能包，也可以使用本激光雷达对应的 ROS 功能包。

依赖

我们已验证本软件包可在以下环境中成功运行：

- `Ubuntu 20.04`

本 SDK 几乎不依赖任何外部依赖库。

但如果你想使用 PCL 点云格式，则需要安装 PCL 库。

配置

使用 USB 线将激光雷达连接到计算机，然后确认激光雷达对应的串口名称：

$ ls /dev/ttyUSB*
/dev/ttyUSB0

默认串口名称为 `/dev/ttyUSB0`。

若实际串口名称与默认值不符，需在 `example_lidar.cpp` 中修改配置参数。

编译

你可以将该程序作为 CMake 项目进行编译：

cd unitree_lidar_sdk

mkdir build

cd build

cmake .. && make -j2

运行

直接运行示例程序：

../bin/example_lidar

输出示例如下：

$ ../bin/example_lidar 
lidar firmware version = 0.3.2+230511
lidar sdk version = 1.0.3

Dirty Percentage = 5.145833 %
Dirty Percentage = 4.166667 %
Dirty Percentage = 4.166667 %

Turn on all the LED lights ...
Turn off all the LED lights ...
Set LED mode to: FORWARD_SLOW ...
Set LED mode to: REVERSE_SLOW ...
Set LED mode to: SIXSTAGE_BREATHING ...

Set Lidar working mode to: NORMAL_MODE ... 

An IMU msg is parsed!
 stamp = 1683874160.559222, id = 729
 quaternion (x, y, z, w) = [0.0131, -0.0091, 0.6888, -0.7225]

An IMU msg is parsed!
 stamp = 1683874160.564979, id = 121
 quaternion (x, y, z, w) = [0.0102, -0.0093, 0.7099, -0.7018]

An IMU msg is parsed!
 stamp = 1683874160.568425, id = 122
 quaternion (x, y, z, w) = [0.0118, -0.0096, 0.7099, -0.7018]

An IMU msg is parsed!
 stamp = 1683874160.573472, id = 123
 quaternion (x, y, z, w) = [0.0126, -0.0093, 0.7098, -0.7018]

An IMU msg is parsed!
 stamp = 1683874160.577348, id = 124
 quaternion (x, y, z, w) = [0.0128, -0.0093, 0.7099, -0.7018]

A Cloud msg is parsed! 
 stamp = 1683874145.535888, id = 1
 cloud size  = 278
 first 10 points (x,y,z,intensity,time,ring) = 
   (-0.029885, -0.136897, 0.000448, 88.000000, 0.000000, 0)
   (-0.035384, -0.171399, 0.005140, 91.000000, 0.000023, 0)
   (-0.043000, -0.219542, 0.012437, 127.000000, 0.000046, 0)
   (-0.054879, -0.294965, 0.024572, 132.000000, 0.000069, 0)
   (-0.055288, -0.301204, 0.033170, 106.000000, 0.000093, 0)
   (-0.054542, -0.300348, 0.041173, 101.000000, 0.000116, 0)
   (-0.053764, -0.299285, 0.049148, 99.000000, 0.000139, 0)
   (-0.056123, -0.318406, 0.060981, 92.000000, 0.000162, 0)
   (-0.055230, -0.316827, 0.069422, 91.000000, 0.000185, 0)
   (-0.051257, -0.294854, 0.072849, 128.000000, 0.000208, 0)
   ...                                                                                                                                                                        

此处，我们会打印点云消息的前 10 个点以及 IMU 消息的四元数。

注意

- 在 Ubuntu 系统中，访问串口设备需要相应的权限。如果你的 C++ 程序没有足够的权限访问串口设备，将会出现 **"Permission denied"**（权限不足）错误。

- 要解决此错误，可以使用以下命令将当前用户添加到 dialout 用户组：

sudo usermod -a -G dialout $USER

- 将用户添加到 dialout 用户组后，需要注销并重新登录，更改才能生效。

如何从 MavLink 消息解析点云

如果你想从直接通过串口获取的 MavLink 消息中解析点云，可以参考以下文档：

- [HowToParsePointCloudAndIMUDataFromMavLinkMessages.md](./doc/HowToParsePointCloudAndIMUDataFromMavLinkMessages.md)

版本历史

v1.0.0（2023.05.04）

- 支持固件版本：0.3.1

v1.0.1（2023.05.05）

- 支持固件版本：0.3.1

- 新增激光雷达工作模式设置支持，例如 `NORMAL_MODE`（正常模式）和 `STANDBY_MODE`（待机模式）

- 新增 LED 灯控制功能

v1.0.2（2023.05.11）

- 支持固件版本：0.3.2

v1.0.3（2023.05.12）

- 支持固件版本：0.3.2

- 新增获取去除脏点百分比的功能

v1.0.4（2023.05.30）

- 支持固件版本：1.0.1

v1.0.5（2023.06.05）

- 支持固件版本：1.0.1

- 将默认 `rotate_yaw_bias`（偏航旋转偏差）更新为校准值 -38.5 度。

- 更新 `README.md`，添加解决打开串口时出现“Permission denied”错误的注意事项。

v1.0.6（2023.06.19）

- 支持固件版本：1.0.1

- 修改 `initialize()` 函数，增加指定串口存在性检查。如果串口名称不存在，初始化失败并返回 -1，而非意外抛出错误。

- 为激光雷达基准平面添加 z 轴偏置

v1.0.7（2023.06.21）

- 支持固件版本：1.0.1

- 在 README 中添加坐标系定义说明

- 修改 `unitree_lidar_ros` 和 `unitree_lidar_ros2` 的说明文档

v1.0.8（2023.06.28）

- 支持固件版本：1.0.1

- 解决依赖问题——`cannot find -llz4`（找不到 lz4 库）

- 添加可选使用的 mavlink 头文件

v1.0.9（2023.06.30）

- 支持固件版本：1.0.1

- 删除固件版本的日期后缀

- 优化激光雷达工作模式设置支持，可在 `NORMAL_MODE`（正常模式）和 `STANDBY_MODE`（待机模式）之间切换

v1.0.10（2023.07.17）

- 支持固件版本：1.0.1

- 新增 C++ 编写的 UDP 发布器，可将激光雷达数据发布到指定 IP 地址和端口

- 新增 C++ 编写的 UDP 订阅器示例，可订阅上述点云和 IMU 数据

- 新增 Python 编写的 UDP 订阅器示例，可订阅上述点云和 IMU 数据

- 为 Windows 用户提供编译安装包，通过该包可在 Windows 系统中获取激光雷达数据。

v1.0.11（2023.07.27）

- 新增说明文档，介绍如何直接从 mavlink 消息解析点云和 IMU 数据。

- 文档路径：`HowToParsePointCloudAndIMUDataFromMavLinkMessages.md`

v1.0.12（2023.09.20）

- 新增 UDP 接口支持，可从指定 UDP 端口解析原始字节，并向激光雷达的 IP 和端口发送指令。

- 更新 `unitree_lidar_ros` 功能包

v1.0.13（2023.10.09）

- 修改 `unitree_lidar_sdk` 中的串口读取方式。调用 `runParse()` 函数时会阻塞，等待串口有数据后再一次性读取数据。

- 支持串口转 UDP 转接板

v1.0.14（2023.10.30）

- 修复使用串口转 UDP 板时偶尔出现的段错误问题。

v1.0.15（2024.01.04）

- 修改 `example_lidar_udp.cpp` 中的默认激光雷达和本地 IP 地址

v1.0.16（2024.02.18）

- 新增关闭 UDP 连接的函数

- 为 `initializeUDP()` 函数设置 1 秒超时
