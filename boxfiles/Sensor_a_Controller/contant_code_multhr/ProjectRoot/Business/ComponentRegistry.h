#ifndef COMPONENT_REGISTRY_H
#define COMPONENT_REGISTRY_H

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <iostream>
#include "../DataAccess/IChannel.h"

namespace BoxSystem {

// 前向声明，不要 include 具体传感器的头文件！
class BaseComponent;

// 统一构造函数签名
using CreatorFunc = std::function<std::shared_ptr<BaseComponent>(
    const std::string& id, 
    const std::string& nodeID, 
    std::shared_ptr<IChannel> channel, 
    uint8_t slaveAddr)>;

class ComponentFactory {
private:
    std::map<std::string, CreatorFunc> registryMap;
    ComponentFactory() = default;

public:
    static ComponentFactory& getInstance() {
        static ComponentFactory instance;
        return instance;
    }

    void registerComponent(const std::string& kind, CreatorFunc func) {
        registryMap[kind] = func;
    }

    std::shared_ptr<BaseComponent> create(const std::string& kind, 
                                        const std::string& id, 
                                        const std::string& nodeID,
                                        std::shared_ptr<IChannel> channel,
                                        uint8_t slaveAddr) {
        if (registryMap.find(kind) != registryMap.end()) {
            return registryMap[kind](id, nodeID, channel, slaveAddr);
        }
        std::cerr << "[错误] 未识别的设备类型: " << kind << std::endl;
        return nullptr;
    }
};

// 注册器模板
template<typename T>
class Registrar {
public:
    Registrar(const std::string& kind) {
        ComponentFactory::getInstance().registerComponent(kind, 
            [](const std::string& id, const std::string& nodeID, std::shared_ptr<IChannel> channel, uint8_t slaveAddr) {
                return std::make_shared<T>(id, nodeID, channel, slaveAddr);
            });
    }
};

} // namespace BoxSystem
#endif