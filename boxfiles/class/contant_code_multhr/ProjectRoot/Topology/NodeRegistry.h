#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include "LogicalNode.h"

namespace BoxSystem {

/**
 * @brief 节点注册中心（单例）
 * 负责管理系统中所有的 PLC 节点和网关节点。
 */
class NodeRegistry {
private:
    // Key: PLC编号 (如 "PLC_01"), Value: 节点对象指针
    std::map<std::string, std::shared_ptr<LogicalNode>> nodes;

    NodeRegistry() = default;

public:
    // 获取全局单例
    static NodeRegistry& getInstance() {
        static NodeRegistry instance;
        return instance;
    }

    /**
     * @brief 注册或获取一个节点
     * 如果编号已存在，则返回现有节点；如果不存在，则创建一个新节点。
     * @param id PLC编号
     * @param info 物理通道信息（如串口路径）
     */
    std::shared_ptr<LogicalNode> getOrCreateNode(const std::string& id, const std::string& info) {
        if (nodes.find(id) == nodes.end()) {
            nodes[id] = std::make_shared<LogicalNode>(id, info);
        }
        return nodes[id];
    }

    // 根据编号查找节点
    std::shared_ptr<LogicalNode> findNode(const std::string& id) {
        auto it = nodes.find(id);
        if (it != nodes.end()) {
            return it->second;
        }
        return nullptr;
    }

    // 获取所有节点的列表（用于主循环遍历执行）
    std::vector<std::shared_ptr<LogicalNode>> getAllNodes() {
        std::vector<std::shared_ptr<LogicalNode>> nodeList;
        for (auto const& [id, node] : nodes) {
            nodeList.push_back(node);
        }
        return nodeList;
    }

    // 清空所有节点（用于重新加载配置文件时）
    void clear() {
        nodes.clear();
    }

    // 禁止拷贝
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;
};

} // namespace BoxSystem

#endif