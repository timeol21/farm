#include <iostream>
#include <memory>
#include <vector>
#include "PLCDevice.h"
#include "Solenoid.h"

int main()
{
    // ==============================================
    // 1. 创建 PLC 设备（对应你的 FxPlcDevice）
    // ==============================================
    std::cout << "\n>>> 初始化 MODBUS PLC..." << std::endl;
    PLCDevice plcDevice(
        1,
        "plc_01",
        "online",
        "/dev/ttyS4",
        -1,
        1
    );

    // ==============================================
    // 2. 创建 PLC 实例（unique_ptr）
    // ==============================================
    auto plcInstance = std::make_unique<PLCDevice>(plcDevice);

    // ==============================================
    // 3. 创建 PLC 管理集合（对应 FxPlcInstanceSet）
    // ==============================================
    // 你可以自己建一个 PLCDeviceSet 类，也可以直接用 unordered_map
    std::unordered_map<std::string, std::unique_ptr<PLCDevice>> plcInstanceSet;

    // ==============================================
    // 4. 添加到集合（move 语义）
    // ==============================================
    plcInstanceSet["plc_01"] = std::move(plcInstance);

    // ==============================================
    // 5. 从集合里拿出 PLC，创建并管理多个电磁阀
    // ==============================================
    auto& currentPlc = plcInstanceSet["plc_01"];

    // 创建多个电磁阀
    auto valve1 = std::make_shared<Solenoid>(1, "S1", "IDLE", "/dev/ttyS4", 1, -1, "valve1", 1);
    auto valve2 = std::make_shared<Solenoid>(2, "S2", "IDLE", "/dev/ttyS3", 1, -1, "valve2", 2);

    // 交给 PLC 内部的 solenoidSet_ 管理
    currentPlc->addSolenoid(std::move(valve1));
    currentPlc->addSolenoid(std::move(valve2));

    // ==============================================
    // 6. 使用
    // ==============================================
    auto valve = currentPlc->getSolenoid(1);
    if (valve) {
        valve->openCurrentSolenoid();
    }

    return 0;
}