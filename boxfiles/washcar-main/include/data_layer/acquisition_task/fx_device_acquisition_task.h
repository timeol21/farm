#pragma once
#include "acquisition_task.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"

class FxDeviceAcquisitionTask : public AcquisitionTask {
public:
    explicit FxDeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet);
    std::vector<DeviceData> execute() override;                      
    int getType() const { return 7; }             
    bool isAcquisitionData() const { return true; }  

private:
    FxPlcInstanceSet& fxPlcSet_;
};