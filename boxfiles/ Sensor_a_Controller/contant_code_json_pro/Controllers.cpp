#include "Controllers.h"

void ValveController::execute() {
    if (!sm.initPort(portPath)) return;
    std::cout << "--- Executing Control: " << name << " (Addr: " << addr << ") ---" << std::endl;
    // 这里可以添加具体的 Modbus 写报文逻辑
}