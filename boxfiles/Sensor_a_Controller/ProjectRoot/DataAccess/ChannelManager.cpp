#include "ChannelManager.h"
#include "SerialChannel.h"
#include <iostream>

namespace BoxSystem {

ChannelManager& ChannelManager::getInstance() {
    static ChannelManager instance;
    return instance;
}

std::shared_ptr<IChannel> ChannelManager::getOrCreateChannel(const LogicalNode& node) {
    std::lock_guard<std::mutex> lock(poolMutex);
    
    // 1. 如果该 NodeID 已经创建过端口，直接返回已有的
    if (channelPool.count(node.getNodeID())) {
        return channelPool[node.getNodeID()];
    }

    // 2. 如果不存在，根据 JSON 传入的路径和波特率创建
    auto newChannel = std::make_shared<SerialChannel>();
    
    // 构造初始化字符串，如 "/dev/ttyS4:9600"
    std::string configStr = node.getPortPath() + ":" + std::to_string(node.getBaudRate());
    
    if (newChannel->open(configStr)) {
        channelPool[node.getNodeID()] = newChannel;
        std::cout << "[ChannelManager] 动态创建端口成功: " << node.getNodeID() 
                  << " -> " << configStr << std::endl;
        return newChannel;
    }

    return nullptr;
}

} // namespace BoxSystem