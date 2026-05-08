#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Topology/LogicalNode.h"
#include "DataAccess/ChannelManager.h"
#include "Business/ComponentRegistry.h"
#include "Business/ThreadManager.h"

using json = nlohmann::json;
using namespace BoxSystem;

int main() {
    std::ifstream f("config.json");
    if (!f) return -1;
    json config = json::parse(f);

    std::vector<std::unique_ptr<LogicalNode>> nodes;

    // 初始化节点和设备
    for (const auto& t : config["topology"]) {
        auto node = std::make_unique<LogicalNode>(t["nodeID"], t["port"], t["baudRate"]);
        auto ch = ChannelManager::getInstance().getOrCreateChannel(*node);
        if (ch) {
            for (const auto& d : config["devices"]) {
                if (d["nodeID"] == t["nodeID"]) {
                    auto comp = ComponentFactory::getInstance().create(d["type"], d["id"], d["nodeID"], ch, d["slaveAddr"]);
                    if (comp) node->addComponent(comp);
                }
            }
            nodes.push_back(std::move(node));
        }
    }

    // 启动独立的多线程管理器
    ThreadManager threadMgr;
    threadMgr.start(nodes);

    std::cout << "系统已进入后台轮询模式。输入 'exit' 退出。" << std::endl;
    std::string input;
    while (std::cin >> input && input != "exit") {
        // 主线程可以处理其他并发请求
    }

    return 0;
}