#include "ModbusComponent.h"
#include "../Business/ComponentRegistry.h" // 只要这一行

namespace BoxSystem {

class WaterLevelSensor : public ModbusComponent {
public:
    // 直接使用父类构造函数
    using ModbusComponent::ModbusComponent;

    void execute() override {
        // ... 业务逻辑 ...
        std::cout << "[" << getIdentity() << "] 水位采集正常" << std::endl;
    }
};

// 静态注册：这行代码会在程序启动时运行
static Registrar<WaterLevelSensor> g_reg_water("WaterLevel");

}