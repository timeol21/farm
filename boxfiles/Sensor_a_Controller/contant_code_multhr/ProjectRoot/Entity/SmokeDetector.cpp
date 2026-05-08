#include "ModbusComponent.h"
#include "../Business/ComponentRegistry.h"
#include <cstring>
#include <iostream>

namespace BoxSystem {

class SmokeDetector : public ModbusComponent {
public:
    // 构造函数：通过初始化列表透传给 ModbusComponent
    SmokeDetector(const std::string& id, const std::string& nodeID, 
                  std::shared_ptr<IChannel> channel, uint8_t slaveAddr)
        : ModbusComponent(id, nodeID, channel, slaveAddr) {}

    void execute() override {
        if (!m_channel) return;

        // Modbus RTU 查询烟雾状态报文示例 (01 04 00 01 00 01 60 0A)
        // 这里的报文内容取决于你的硬件协议
        unsigned char sendBuf[] = {m_slaveAddr, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
        // 简易计算校验位逻辑或直接硬编码
        
        m_channel->send(sendBuf, 8);
        
        // 模拟接收数据
        unsigned char recvBuf[256];
        int len = m_channel->receive(recvBuf, sizeof(recvBuf));

        if (len > 0) {
            std::cout << "[" << getIdentity() << "] 烟雾探测器轮询正常，当前浓度在安全范围内。" << std::endl;
        } else {
            std::cout << "[" << getIdentity() << "] 烟雾探测器响应超时！" << std::endl;
        }
    }
};

// 核心：自注册到工厂，JSON 中 type 填 "Smoke"
static Registrar<SmokeDetector> g_regSmoke("Smoke");

} // namespace BoxSystem