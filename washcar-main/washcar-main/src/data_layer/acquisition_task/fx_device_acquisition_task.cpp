#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "common/config/config_object.h"
#include "common/config/point.h"
#include "common/log/log_manager.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <algorithm>

using namespace washcar::cfg;

FxDeviceAcquisitionTask::FxDeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet,
                                                   const washcar::cfg::MachinePointTable& table)
    : AcquisitionTask(7, 1), fxPlcSet_(fxPlcSet), table_(table) {}

std::vector<DeviceData> FxDeviceAcquisitionTask::execute() {
    std::vector<DeviceData> result;

    // 采哪些软元件由构造时传入的软元件表决定(见 device_acquisition_task.cpp 的装配处)。
    const auto& acquisition = table_.acquisition;

    for (auto& [plcId, plc] : fxPlcSet_.getAll()) {
        if (!plc->connect()) {
            LOG_WARNING(std::string("[FxDeviceAcquisitionTask] acquisition: PLC ") + plcId
                        + " 连接失败, skip this cycle");
            continue;
        }

        FxPlcStatus status(plcId);

        // ---- D 寄存器:逐个读(每个 D 独立地址,不能合) ----
        for (const auto& point : acquisition.dPoints) {
            const auto parsed = parsePoint(point);
            if (parsed.type != 'D') continue;
            uint16_t value = 0;
            if (plc->readD(parsed.addr, value)) {
                status.updateDRegister(parsed.addr, value);
            } else {
                logReadFailure(plcId, point);
            }
        }

        // ---- Y 点:逐个读 ----
        for (const auto& point : acquisition.yPoints) {
            const auto parsed = parsePoint(point);
            if (parsed.type != 'Y') continue;
            bool state = false;
            if (plc->readYBit(parsed.addr, state)) {
                status.updateYBit(parsed.addr, state);
            } else {
                logReadFailure(plcId, point);
            }
        }

        // ---- X 点:逐个读 ----
        for (const auto& point : acquisition.xPoints) {
            const auto parsed = parsePoint(point);
            if (parsed.type != 'X') continue;
            bool state = false;
            if (plc->readXBit(parsed.addr, state)) {
                status.updateXBit(parsed.addr, state);
            } else {
                logReadFailure(plcId, point);
            }
        }

        // ---- M 点:逐个读 ----
        for (const auto& point : acquisition.mPoints) {
            const auto parsed = parsePoint(point);
            if (parsed.type != 'M') continue;
            bool state = false;
            if (plc->readM(parsed.addr, state)) {
                status.updateMBit(parsed.addr, state);
            } else {
                logReadFailure(plcId, point);
            }
        }

        // ---- S 点:逐个读 ----
        for (const auto& point : acquisition.sPoints) {
            const auto parsed = parsePoint(point);
            if (parsed.type != 'S') continue;
            bool state = false;
            if (plc->readS(parsed.addr, state)) {
                status.updateSBit(parsed.addr, state);
            } else {
                logReadFailure(plcId, point);
            }
        }

        status.updateTimestamp();
        DeviceData data(7, status);
        result.push_back(data);
    }

    return result;
}
