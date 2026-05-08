#include "ModbusComponent.h"
#include "../Business/ComponentRegistry.h"
#include <cstring>

namespace BoxSystem {
class WaterLevelSensor : public ModbusComponent {
public:
    WaterLevelSensor(const std::string& id, const std::string& nodeID, std::shared_ptr<IChannel> ch, uint8_t addr)
        : ModbusComponent(id, nodeID, ch, addr) {}

    void execute() override {
        unsigned char cmd[] = {m_slaveAddr, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};
        m_channel->send(cmd, 8);
        std::cout << "[" << getIdentity() << "] 正在读取水位..." << std::endl;
    }
};
static Registrar<WaterLevelSensor> g_regWater("WaterLevel");
}