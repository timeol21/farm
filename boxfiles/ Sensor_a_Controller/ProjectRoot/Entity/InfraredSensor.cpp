#include "ModbusComponent.h"
#include "../Business/ComponentRegistry.h"
#include <cstring>

namespace BoxSystem {
class InfraredSensor : public ModbusComponent {
    uint16_t m_reg;
public:
    InfraredSensor(const std::string& id, const std::string& nodeID, std::shared_ptr<IChannel> ch, uint8_t addr, uint16_t reg)
        : ModbusComponent(id, nodeID, ch, addr), m_reg(reg) {}

    void execute() override {
        std::cout << "[" << getIdentity() << "] 正在轮询红外(寄存器: " << std::hex << m_reg << ")..." << std::endl;
    }
};

// 特殊注册：针对 X2 和 X3 传递不同的寄存器地址
static bool regX2 = [](){
    ComponentFactory::getInstance().registerComponent("InfraredX2", [](const std::string& id, const std::string& nodeID, std::shared_ptr<IChannel> ch, uint8_t addr){
        return std::make_shared<InfraredSensor>(id, nodeID, ch, addr, 0x0402);
    });
    return true;
}();

static bool regX3 = [](){
    ComponentFactory::getInstance().registerComponent("InfraredX3", [](const std::string& id, const std::string& nodeID, std::shared_ptr<IChannel> ch, uint8_t addr){
        return std::make_shared<InfraredSensor>(id, nodeID, ch, addr, 0x0403);
    });
    return true;
}();
}