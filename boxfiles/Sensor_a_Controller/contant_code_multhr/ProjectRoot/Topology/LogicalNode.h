#ifndef LOGICAL_NODE_H
#define LOGICAL_NODE_H

#include <string>
#include <vector>
#include <memory>
#include "../Entity/BaseComponent.h"

namespace BoxSystem {

class LogicalNode {
private:
    std::string nodeID;        // 节点编号（如 COM_BUS_1）
    std::string portPath;      // 串口路径（如 /dev/ttyS4）
    int baudRate;              // 波特率（如 9600）
    
    std::vector<std::shared_ptr<BaseComponent>> components;

public:
    // 构造函数：增加波特率参数
    LogicalNode(const std::string& id, const std::string& path, int baud)
        : nodeID(id), portPath(path), baudRate(baud) {}

    void addComponent(std::shared_ptr<BaseComponent> comp) {
        if (comp) components.push_back(comp);
    }

    void executeAll() {
        for (auto& comp : components) comp->execute();
    }

    // 给 ChannelManager 使用的接口
    std::string getNodeID() const { return nodeID; }
    std::string getPortPath() const { return portPath; }
    int getBaudRate() const { return baudRate; }
};

} // namespace BoxSystem
#endif