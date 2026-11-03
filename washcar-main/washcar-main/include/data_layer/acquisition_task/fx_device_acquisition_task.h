#pragma once
#include "acquisition_task.h"
#include "common/config/config_object.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"

// 海天(FX 串口)设备点位采集 Task。
// 采哪些软元件由构造时传入的软元件表决定 —— 采集层不认"点位表"这个概念,
// 只认"给我一张点位表,我按表采"。
class FxDeviceAcquisitionTask : public AcquisitionTask {
public:
    explicit FxDeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet,
                                      const washcar::cfg::MachinePointTable& table);
    std::vector<DeviceData> execute() override;
    int getType() const { return 7; }
    bool isAcquisitionData() const { return true; }

private:
    FxPlcInstanceSet& fxPlcSet_;
    // 这台洗车机的软元件表:采哪些点、哪些点要打变化日志
    const washcar::cfg::MachinePointTable& table_;
};
