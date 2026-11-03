#pragma once
#include "acquisition_task.h"
#include "common/config/config_object.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"

// 驿公里洗车机设备点位采集 Task
// - 实际遍历 FxPlcInstanceSet 全部 PLC,代码层无 plcId/boxType 过滤
// - 当前安全前提:main_test.cpp 海天/驿公里互斥 if-else,fxPlcSet_ 只装 1 个 PLC,其它 PLC 不会进来
// - 采哪些软元件由构造时传入的软元件表决定 —— 采集层不认"点位表"这个概念,
//   只认"给我一张点位表,我按表采"
// - 跟 FxDeviceAcquisitionTask 共用 type=7, 内部按 plcId 过滤避免重复
// 参考: docs/驿公里_MQTT_协议对接总结.md
class TcpPlcDeviceAcquisitionTask : public AcquisitionTask {
public:
	explicit TcpPlcDeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet,
	                                    const washcar::cfg::MachinePointTable& table);
	std::vector<DeviceData> execute() override;
	int getType() const { return 7; }
	bool isAcquisitionData() const { return true; }

private:
	FxPlcInstanceSet& fxPlcSet_;
	// 这台洗车机的软元件表:采哪些点、哪些点要打变化日志
	const washcar::cfg::MachinePointTable& table_;
};
