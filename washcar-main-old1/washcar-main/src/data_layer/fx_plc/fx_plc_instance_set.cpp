#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/device/device_data.h"
#include "common/log/log_manager.h"

void FxPlcInstanceSet::addPlc(std::unique_ptr<IPlcInstance> plc) {
    if (plc) {
        plcMap_[plc->getPlcId()] = std::move(plc);
    }
}

IPlcInstance* FxPlcInstanceSet::getPlc(const std::string& plcId) {
    auto it = plcMap_.find(plcId);
    if (it != plcMap_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const std::unordered_map<std::string, std::unique_ptr<IPlcInstance>>& FxPlcInstanceSet::getAll() const {
    return plcMap_;
}
std::vector<FxPlcStatus> FxPlcInstanceSet::getFxPlcStatusList() const {
    std::vector<FxPlcStatus> result;
    for (const auto& [plcId, plc] : plcMap_) {
        if (!plc->connect()) {
            std::cerr << "[FxPlcInstanceSet] PLC " << plcId << " 连接失败，跳过状态读取" << std::endl;
            LOG_ERROR("FX PLC [" + plcId + "] 连接失败，跳过状态读取");
            continue;
        }

        FxPlcStatus status(plcId);

        // 读取 Y 点（参考 FxDeviceAcquisitionTask）
        std::vector<int> yPoints = {50, 51, 52, 74,55};
        for (int y : yPoints) {
            bool state = false;
            if (plc->readYBit(y, state)) {
                status.updateYBit(y, state);
            }
        }

        std::vector<int> dRegs = {142, 150};
        for (int d : dRegs) {
            uint16_t value = 0;
            if (plc->readD(d, value)) {
                status.updateDRegister(d, value);
            }
        }

        //故障点位
        // std::vector<int> faultMPoints = {220, 221, 223, 224, 225, 400, 47, 250, 251, 257, 258, 259, 381,
        //                                  1, 187, 500, 100, 0, 2070, 1023, 517, 247, 515, 26, 227, 208,
        //                                  183, 230};
         std::vector<int> faultMPoints = {1, 100, 0, 2070, 616};
        for (int m : faultMPoints) {
            bool state = false;
            if (plc->readM(m, state)) {
                status.updateMBit(m, state);
                if (m == 616) {
                    LOG_DEBUG(std::string("[FxPlcInstanceSet] PLC ") + plcId + " 读取 M616=" + (state ? "1" : "0"));
                }
            }
        }

        // std::vector<int> faultXPoints = {44, 50, 20, 32, 33, 34, 35, 36, 37, 40, 41, 42, 43, 47, 45};
        // for (int x : faultXPoints) {
        //     bool state = false;
        //     if (plc->readXBit(x, state)) {
        //         status.updateXBit(x, state);
        //     }
        // }

        std::vector<int> faultSPoints = {0};
        for (int s : faultSPoints) {
            bool state = false;
            if (plc->readS(s, state)) {
                status.updateSBit(s, state);
            }
        }

        status.updateTimestamp();
        result.push_back(status);
    }
    return result;
}

std::vector<DeviceData> FxPlcInstanceSet::acquisitionFxPlcData() {
    std::vector<DeviceData> result;
    for (const auto& [plcId, plc] : plcMap_) {
        if (!plc->connect()) {
            std::cerr << "[FxPlcInstanceSet] PLC " << plcId << " 连接失败，跳过采集" << std::endl;
            continue;
        }

        FxPlcStatus status(plcId);

        // 读取基本点位（与 FxDeviceAcquisitionTask 保持一致）
        // std::vector<int> yPoints = {50, 51, 52, 74};
        // for (int y : yPoints) {
        //     bool state = false;
        //     if (plc->readYBit(y, state)) {
        //         status.updateYBit(y, state);
        //     }
        // }

        std::vector<int> dRegs = {142, 150};
        for (int d : dRegs) {
            uint16_t value = 0;
            if (plc->readD(d, value)) {
                status.updateDRegister(d, value);
            }
        }


        std::vector<int> faultMPoints = {1,100,0,2070,616};
        for (int m : faultMPoints) {
            bool state = false;
            if (plc->readM(m, state)) {
                status.updateMBit(m, state);
                if (m == 616) {
                    LOG_DEBUG(std::string("[FxPlcInstanceSet] PLC ") + plcId + " 读取 M616=" + (state ? "1" : "0"));
                }
            }
        }

        // std::vector<int> faultXPoints = {44, 50, 20, 32, 33, 34, 35, 36, 37, 40, 41, 42, 43, 47, 45};
        // for (int x : faultXPoints) {
        //     bool state = false;
        //     if (plc->readXBit(x, state)) {
        //         status.updateXBit(x, state);
        //     }
        // }

        bool s0State = false;
        if (plc->readS(0, s0State)) {
            status.updateSBit(0, s0State);
        }

        status.updateTimestamp();
        result.emplace_back(7, status);   // deviceType = 7 表示 FX PLC
    }
    return result;
}
