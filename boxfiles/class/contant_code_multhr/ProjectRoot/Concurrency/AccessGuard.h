#ifndef ACCESS_GUARD_H
#define ACCESS_GUARD_H

#include <mutex>
#include <map>
#include <string>
#include <memory>

namespace BoxSystem {

/**
 * @brief 访问守卫类（单例）
 * 专门负责管理物理资源的互斥访问，将多线程逻辑从业务中剥离。
 */
class AccessGuard {
private:
    // 为每一个物理路径分配一个独立的互斥锁
    // Key: 通道路径 (如 "/dev/ttyS4"), Value: 对应的锁
    std::map<std::string, std::shared_ptr<std::mutex>> lockMap;
    std::mutex mapMutex; // 保护 lockMap 自身的安全

    AccessGuard() = default;

public:
    static AccessGuard& getInstance() {
        static AccessGuard instance;
        return instance;
    }

    /**
     * @brief 获取指定通道的锁
     */
    std::shared_ptr<std::mutex> getLock(const std::string& channelPath) {
        std::lock_guard<std::mutex> lock(mapMutex);
        if (lockMap.find(channelPath) == lockMap.end()) {
            lockMap[channelPath] = std::make_shared<std::mutex>();
        }
        return lockMap[channelPath];
    }

    /**
     * @brief 自动执行工具（RAII方式）
     * 传入通道路径和一段逻辑，自动加锁、执行、解锁
     */
    template<typename Func>
    void syncExecute(const std::string& channelPath, Func task) {
        auto mtx = getLock(channelPath);
        std::lock_guard<std::mutex> lock(*mtx);
        task(); // 执行具体的业务逻辑（如发送并接收数据）
    }
};

} // namespace BoxSystem

#endif