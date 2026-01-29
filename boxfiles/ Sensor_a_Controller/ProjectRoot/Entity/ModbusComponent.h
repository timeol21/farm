#ifndef MODBUS_COMPONENT_H
#define MODBUS_COMPONENT_H

#include "BaseComponent.h"
#include "../DataAccess/IChannel.h"
#include <memory>
#include <cstdint>

namespace BoxSystem {

class ModbusComponent : public BaseComponent {
protected:
    std::shared_ptr<IChannel> m_channel;
    uint8_t m_slaveAddr;

public:
    ModbusComponent(const std::string& id, const std::string& nodeID, 
                    std::shared_ptr<IChannel> channel, uint8_t slaveAddr)
        : BaseComponent(id, nodeID), m_channel(channel), m_slaveAddr(slaveAddr) {}
};

} 
#endif