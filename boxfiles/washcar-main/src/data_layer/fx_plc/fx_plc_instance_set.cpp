#include "data_layer/fx_plc/fx_plc_instance_set.h"

void FxPlcInstanceSet::addPlc(std::unique_ptr<FxPlcInstance> plc) {
    if (plc) {
        plcMap_[plc->getPlcId()] = std::move(plc);
    }
}

FxPlcInstance* FxPlcInstanceSet::getPlc(const std::string& plcId) {
    auto it = plcMap_.find(plcId);
    if (it != plcMap_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const std::unordered_map<std::string, std::unique_ptr<FxPlcInstance>>& FxPlcInstanceSet::getAll() const {
    return plcMap_;
}