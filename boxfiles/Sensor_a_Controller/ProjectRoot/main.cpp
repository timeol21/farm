#include <iostream>
#include <fstream>
#include <thread>
// #include <nlohmann/json.hpp>
#include "json.hpp"
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

/*
g++ -g *.cpp -o main_debug -I. -lpthread
g++ -g *.cpp DataAccess/*.cpp -o main_debug -I. -I./DataAccess -lpthread
已经改好了 tasks.json，你其实不需要手动敲命令了：

打开 main.cpp（确保它是当前活动窗口）。
按下键盘上的 F5。
VS Code 会自动运行 tasks.json 里的那堆复杂的命令（它会去 DataAccess 下找所有 .cpp等等，文件夹下包含子文件件的所有的.cpp都编译好）。
*/