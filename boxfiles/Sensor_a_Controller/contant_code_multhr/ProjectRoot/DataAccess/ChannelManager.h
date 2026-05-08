#ifndef CHANNEL_MANAGER_H
#define CHANNEL_MANAGER_H

#include <map>
#include <string>
#include <memory>
#include <mutex>
#include "IChannel.h"
#include "../Topology/LogicalNode.h" // 引入拓扑定义

namespace BoxSystem {

class ChannelManager {
private:
    // 使用 std::shared_ptr 管理通道，Key 为 NodeID (如 COM_BUS_1)
    std::map<std::string, std::shared_ptr<IChannel>> channelPool;
    std::mutex poolMutex;

    ChannelManager() = default;

public:
    static ChannelManager& getInstance();

    /**
     * @brief 根据逻辑节点配置动态获取或创建通道
     */
    std::shared_ptr<IChannel> getOrCreateChannel(const LogicalNode& node);

    // 禁止拷贝
    ChannelManager(const ChannelManager&) = delete;
    ChannelManager& operator=(const ChannelManager&) = delete;
};

} // namespace BoxSystem
#endif