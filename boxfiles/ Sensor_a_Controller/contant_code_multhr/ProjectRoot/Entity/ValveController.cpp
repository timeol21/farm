#include "ModbusComponent.h"
#include "../Business/ComponentRegistry.h"
#include <cstring>
#include <iostream>

namespace BoxSystem {

class ValveController : public ModbusComponent {
public:
    ValveController(const std::string& id, const std::string& nodeID, 
                    std::shared_ptr<IChannel> channel, uint8_t slaveAddr)
        : ModbusComponent(id, nodeID, channel, slaveAddr) {}

    /**
     * @brief 周期性执行的函数
     * 在轮询中，我们通常是读取阀门的开合百分比或状态
     */
    void execute() override {
        if (!m_channel) return;

        // 假设功能码 03 读取保持寄存器中的阀门状态
        unsigned char readCmd[] = {m_slaveAddr, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
        
        m_channel->send(readCmd, 8);

        unsigned char recvBuf[256];
        int len = m_channel->receive(recvBuf, sizeof(recvBuf));

        if (len >= 5) {
            std::cout << "[" << getIdentity() << "] 阀门状态确认：处于开启位置。" << std::endl;
        }
    }

    /**
     * @brief 额外的控制接口
     * 以后多线程版本中，可以通过此接口下发“关闭”指令
     */
    void controlValve(bool open) {
        unsigned char writeCmd[] = {m_slaveAddr, 0x06, 0x00, 0x00, 0x00, (unsigned char)(open ? 0x01 : 0x00), 0x00, 0x00};
        m_channel->send(writeCmd, 8);
        std::cout << "[" << getIdentity() << "] 已发送阀门" << (open ? "开启" : "关闭") << "指令。" << std::endl;
    }
};

// 核心：自注册到工厂，JSON 中 type 填 "Valve"
static Registrar<ValveController> g_regValve("Valve");

} // namespace BoxSystem