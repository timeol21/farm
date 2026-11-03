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

// 按组读点位:一次串口往返回该组的 8 个位,组内每个点各取一位。
// X / Y 都是 group = addr/10, bit = addr%10(见 FxPlcInstance::readXBit / readYBit)。
// 组读一次只回 1 字节,组内只有 bit 0~7 有效 —— bit 8 / 9 读不出来。
// 按组读是省往返的关键:X 30 个点只要 8 次读,不是 30 次。
// 读失败的组整个不出现在 out 里,由调用方逐点报告警。
static void readGroupBits(IPlcInstance* plc, char type,
                           const std::vector<int>& addresses,
                           std::unordered_map<int, bool>& out) {
    std::map<int, std::vector<int>> groups;   // group -> 组内点位
    for (int addr : addresses) groups[addr / 10].push_back(addr);

    for (const auto& [group, members] : groups) {
        int status8 = 0;
        const bool ok = (type == 'X') ? plc->readX(group, status8)
                                      : plc->readY(group, status8);
        if (!ok) continue;
        for (int addr : members) {
            out[addr] = ((status8 >> (addr % 10)) & 1) != 0;
        }
    }
}

std::vector<DeviceData> FxDeviceAcquisitionTask::execute() {
    std::vector<DeviceData> result;

    // 采哪些软元件由构造时传入的软元件表决定(见 device_acquisition_task.cpp 的装配处)。
    const auto& acquisition = table_.acquisition;

    for (auto& [plcId, plc] : fxPlcSet_.getAll()) {
        if (!plc->connect()) {
            // LOG_WARNING(std::string("[FxDeviceAcquisitionTask] acquisition: PLC ") + plcId + " 连接失败, skip this cycle");
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

        // ---- Y 点:按组读(一次一组) ----
        {
            std::vector<int> addresses;
            for (const auto& point : acquisition.yPoints) {
                const auto parsed = parsePoint(point);
                if (parsed.type == 'Y') addresses.push_back(parsed.addr);
            }
            std::unordered_map<int, bool> states;
            readGroupBits(plc.get(), 'Y', addresses, states);
            for (const auto& [addr, value] : states) {
                status.updateYBit(addr, value);
            }
            for (int addr : addresses) {
                if (!states.count(addr)) logReadFailure(plcId, "Y" + std::to_string(addr));
            }
        }

        // ---- X 点:按组读(一次一组) ----
        {
            std::vector<int> addresses;
            for (const auto& point : acquisition.xPoints) {
                const auto parsed = parsePoint(point);
                if (parsed.type == 'X') addresses.push_back(parsed.addr);
            }
            std::unordered_map<int, bool> states;
            readGroupBits(plc.get(), 'X', addresses, states);
            for (const auto& [addr, value] : states) {
                status.updateXBit(addr, value);
            }
            for (int addr : addresses) {
                if (!states.count(addr)) logReadFailure(plcId, "X" + std::to_string(addr));
            }
        }

        // ---- M 点:逐个读 ----
        // readM 一次只回一位,同一 word 里的多个 M 点会重复读同一个 word。
        // 批量读要动协议层接口,不在本次范围内。
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

        logKeyStateChanges(table_, status, plcId);

        status.updateTimestamp();
        DeviceData data(7, status);
        result.push_back(data);
    }

    return result;
}
