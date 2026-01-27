#include <iostream>
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>
#include "Topology/LogicalNode.h"
#include "DataAccess/ChannelManager.h"
#include "Business/ComponentRegistry.h"

using json = nlohmann::json;
using namespace BoxSystem;

int main() {
    std::ifstream f("config.json");
    if (!f) return -1;
    json config = json::parse(f);

    std::vector<std::unique_ptr<LogicalNode>> nodes;

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

    while (true) {
        for (auto& n : nodes) n->executeAll();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}