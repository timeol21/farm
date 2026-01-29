#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "../Topology/LogicalNode.h"

namespace BoxSystem {
class ThreadManager {
private:
    std::vector<std::thread> workers;
    std::atomic<bool> stopFlag{false};

public:
    ThreadManager() = default;
    ~ThreadManager() { stop(); }

    // 为每个节点分配一个线程进行独立轮询
    void start(std::vector<std::unique_ptr<LogicalNode>>& nodes) {
        stopFlag = false;
        for (auto& node : nodes) {
            workers.emplace_back([this, &node]() {
                while (!stopFlag) {
                    node->executeAll();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            });
        }
    }

    void stop() {
        stopFlag = true;
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
        workers.clear();
    }
};
}
#endif