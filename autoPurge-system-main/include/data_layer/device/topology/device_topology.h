#pragma once

#include "data_layer/device/device_data_object.h"
#include <memory>
#include <vector>
#include <unordered_map>
//定义 设备在系统里面的结构节点 
struct DeviceNode { 
    DeviceDefinition definition;

    std::shared_ptr<HostNodeDefinition> host; //宿主节点

    std::shared_ptr<InterfaceDefinition> interface; 

    std::weak_ptr<DeviceNode> parent;

    std::vector<std::shared_ptr<DeviceNode>> children;

};

struct DeviceTopology { 
    std::unordered_map<std::string,std::shared_ptr<HostNodeDefinition>> hostNodes;

    std::unordered_map<std::string,std::shared_ptr<InterfaceDefinition>> interfaceNodes;

    std::unordered_map<std::string,std::shared_ptr<DeviceNode>> deviceNodes;
   
};