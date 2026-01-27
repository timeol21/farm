#ifndef COMPONENT_REGISTRY_H
#define COMPONENT_REGISTRY_H

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <iostream>
#include "../DataAccess/IChannel.h"

namespace BoxSystem {

class BaseComponent;

// 统一工厂函数签名：4个参数
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
        if (registryMap.count(kind)) return registryMap[kind](id, nodeID, channel, slaveAddr);
        return nullptr;
    }
};

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

} 
#endif