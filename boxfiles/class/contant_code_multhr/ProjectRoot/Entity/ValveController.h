#ifndef VALVE_CONTROLLER_H
#define VALVE_CONTROLLER_H

#include "BaseComponent.h"
#include "../DataAccess/IChannel.h"
#include <memory>

namespace BoxSystem {

class ValveController : public BaseComponent {
private:
    int modbusAddr; // 该电磁阀在 PLC 里的具体寄存器地址
    
public:
    // 构造函数：接受组件ID和所属PLC节点ID
    ValveController(const std::string& id, const std::string& nodeID);

    // 实现核心业务逻辑
    void execute() override;

    // 业务特有函数：设置寄存器地址（由工厂调用）
    void setModbusAddr(int addr) { modbusAddr = addr; }
};

} // namespace BoxSystem

#endif