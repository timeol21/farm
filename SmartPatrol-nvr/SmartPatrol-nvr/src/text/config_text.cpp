#include "data_layer/config_parser.h"
#include <string>
#include <iostream>  // 必须引入，用于 std::cout 打印

// 辅助打印函数
void printLine() {
    std::cout << "---------------------------------------------\n";
}

int main() {
    // 1. 加载配置文件
    bool loadSuccess = ConfigParser::getInstance().loadFromFile("/home/ztl/workspace/SmartPatrol-nvr/SmartPatrol-nvr/config/config.json");
    if (!loadSuccess) {
        std::cerr << "Error: 配置文件加载失败！" << std::endl;
        return -1;
    }
    std::cout << "配置文件加载成功！" << std::endl;
    printLine();

    // 2. 获取配置数据
    const auto& cfg = ConfigParser::getInstance().getConfig();

    // 3. 打印配置根信息
    std::cout << "配置版本: " << cfg.version << std::endl;
    std::cout << "配置描述: " << cfg.description << std::endl;
    printLine();

    // 4. 打印 NVR 配置
    const auto& nvr = cfg.nvr;
    std::cout << "NVR 配置信息:" << std::endl;
    std::cout << "NVR ID: " << nvr.nvrId << std::endl;
    std::cout << "品牌: " << nvr.brand << std::endl;
    std::cout << "IP 地址: " << nvr.ip << std::endl;
    std::cout << "用户名: " << nvr.username << std::endl;
    std::cout << "密码: " << nvr.password << std::endl;
    std::cout << "端口: " << nvr.port << std::endl;
    printLine();

    // 5. 打印摄像头配置（含通道号）
    std::cout << "摄像头配置列表 (共 " << cfg.cameras.size() << " 个):" << std::endl;
    for (const auto& camera : cfg.cameras) {
        std::cout << "摄像头 ID: " << camera.cameraId << std::endl;
        std::cout << "关联 NVR ID: " << camera.nvrId << std::endl;
        std::cout << "摄像头名称: " << camera.name << std::endl;
        std::cout << "分配通道号: " << camera.channelNo << std::endl;  // 假设 CameraConfig 已添加 channelNo 字段
        printLine();
    }
    return 0;
}