#pragma once
#include <vector>
#include <memory>
#include "data_layer/device/device_data_object.h"
#include "data_layer/device/topology/topology_builder.h"
#include <sstream>

struct DeviceAccessPath {
    std::string fullPath;        // 完整唯一路径：/host/ip/interface/endpoint/device/address
    std::string hostId;         // 宿主ID
    std::string hostIp;          // 宿主IP
    std::string interfaceType;  // 接口类型
    std::string interfaceEndpoint; // 接口端点
    std::string deviceId;       // 设备ID
    std::string deviceAddress;  // 设备地址
    std::string parentPath;     // 父设备路径（层级结构）

    // 工具：拼接完整路径
    void buildFullPath() {
        std::stringstream ss;
        if (!parentPath.empty()) {
            ss << parentPath;
        }
        ss << "/" << hostId << "/" << interfaceEndpoint << "/" << deviceId;
        if (!deviceAddress.empty()) {
            ss << "/" << deviceAddress;
        }
        fullPath = ss.str();
    }
};

class DeviceAccessPathResolver {
public:
    DeviceAccessPath resolve(const std::shared_ptr<DeviceNode>& node) const;

private:
    // 辅助：递归解析父设备路径
    std::string resolveParentPath(const std::weak_ptr<DeviceNode>& parentNode) const;
    // 辅助：InterfaceType 转字符串
    std::string interfaceTypeToString(InterfaceType type) const;    
};