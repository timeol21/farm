#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include <iostream>

FxDeviceAcquisitionTask::FxDeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet)
    : AcquisitionTask(7, 1), fxPlcSet_(fxPlcSet) {}

std::vector<DeviceData> FxDeviceAcquisitionTask::execute() {
    std::vector<DeviceData> result;

    for (auto& [plcId, plc] : fxPlcSet_.getAll()) {
        if (!plc->connect()) {
            std::cerr << "[FX] PLC " << plcId << " 连接失败" << std::endl;
            continue;
        }

        FxPlcStatus status(plcId);

        // 读取 Y 点
        std::vector<int> yPoints = {50, 51, 52, 74};
        for (int y : yPoints) {
            bool state = false;
            if (plc->readYBit(y, state)) {
                status.updateYBit(y, state);
                // std::cout << "[采集] " << plcId << " Y" << std::oct << y << std::dec 
                //           << " = " << (state ? "ON" : "OFF") << std::endl;
            }
        }

        // 读取 M 点
        std::vector<int> mPoints = {1, 187};
        for (int m : mPoints) {
            bool state = false;
            if (plc->readM(m, state)) {
                status.updateMBit(m, state);
                // std::cout << "[采集] " << plcId << " M" << m 
                //           << " = " << (state ? "ON" : "OFF") << std::endl;
            }
        }

        // 读取 D 寄存器
        std::vector<int> dRegs = {142};
        for (int d : dRegs) {
            uint16_t value = 0;
            if (plc->readD(d, value)) {
                status.updateDRegister(d, value);
                // std::cout << "[采集] " << plcId << " D" << d << " = " << value << std::endl;
            }
        }

        DeviceData data(7, status);
        result.push_back(data);
    }

    return result;
}