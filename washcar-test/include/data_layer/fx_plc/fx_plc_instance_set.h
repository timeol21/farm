#pragma once
#include "data_layer/fx_plc/iplc_instance.h"
#include "data_layer/fx_plc/fx_plc_status.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include "data_layer/device/device_data.h"

class FxPlcInstanceSet {
public:
    void addPlc(std::unique_ptr<IPlcInstance> plc);
    IPlcInstance* getPlc(const std::string& plcId);
    const std::unordered_map<std::string, std::unique_ptr<IPlcInstance>>& getAll() const;
    //获取所有
    std::vector<FxPlcStatus> getFxPlcStatusList() const;
    //统一采集 到devicedata
    std::vector<DeviceData> acquisitionFxPlcData();

private:
    std::unordered_map<std::string, std::unique_ptr<IPlcInstance>> plcMap_;
};
