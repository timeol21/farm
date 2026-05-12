#include "data_layer/fx_plc/fx_plc_status.h"

FxPlcStatus::FxPlcStatus(const std::string& plcId)
    : DeviceStatus(plcId, 7, "FX_PLC"), plcId_(plcId) {}

void FxPlcStatus::updateYBit(int yOctal, bool state) {
    yStatusMap_[yOctal] = state;
}

void FxPlcStatus::updateMBit(int mDecimal, bool state) {
    mStatusMap_[mDecimal] = state;
}

void FxPlcStatus::updateDRegister(int dNumber, uint16_t value) {
    dRegisterMap_[dNumber] = value;
}

bool FxPlcStatus::getYBit(int yOctal, bool& state) const {
    auto it = yStatusMap_.find(yOctal);
    if (it != yStatusMap_.end()) {
        state = it->second;
        return true;
    }
    return false;
}

bool FxPlcStatus::getMBit(int mDecimal, bool& state) const {
    auto it = mStatusMap_.find(mDecimal);
    if (it != mStatusMap_.end()) {
        state = it->second;
        return true;
    }
    return false;
}

bool FxPlcStatus::getDRegister(int dNumber, uint16_t& value) const {
    auto it = dRegisterMap_.find(dNumber);
    if (it != dRegisterMap_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

//新增y点
void FxPlcStatus::updateXBit(int xOctal, bool state) {
    xStatusMap_[xOctal] = state;
}

bool FxPlcStatus::getXBit(int xOctal, bool& state) const {
    auto it = xStatusMap_.find(xOctal);
    if (it != xStatusMap_.end()) {
        state = it->second;
        return true;
    }
    return false;
}
//s点实现
void FxPlcStatus::updateSBit(int sDecimal, bool state) {
    sStatusMap_[sDecimal] = state;
}

//s点实现
bool FxPlcStatus::getSBit(int sDecimal, bool& state) const {
    auto it = sStatusMap_.find(sDecimal);
    if (it != sStatusMap_.end()) {
        state = it->second;
        return true;
    }
    return false;
}


void FxPlcStatus::updateFrom(const FxPlcStatus& other) {
    // 合并 Y 点
    for (const auto& [addr, val] : other.yStatusMap_) {
        yStatusMap_[addr] = val;
    }
    // 合并 M 点
    for (const auto& [addr, val] : other.mStatusMap_) {
        mStatusMap_[addr] = val;
    }
    // 合并 D 寄存器
    for (const auto& [addr, val] : other.dRegisterMap_) {
        dRegisterMap_[addr] = val;
    }
    // 合并 X 点
    for (const auto& [addr, val] : other.xStatusMap_) {
        xStatusMap_[addr] = val;
    }
    // 合并 S 点
    for (const auto& [addr, val] : other.sStatusMap_) {
        sStatusMap_[addr] = val;
    }
}