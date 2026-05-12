#pragma once
#include "data_layer/fx_plc/fx_plc_instance.h"
#include <unordered_map>
#include <memory>
#include <string>

class FxPlcInstanceSet {
public:
    void addPlc(std::unique_ptr<FxPlcInstance> plc);
    FxPlcInstance* getPlc(const std::string& plcId);
    const std::unordered_map<std::string, std::unique_ptr<FxPlcInstance>>& getAll() const;

private:
    std::unordered_map<std::string, std::unique_ptr<FxPlcInstance>> plcMap_;
};