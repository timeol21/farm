#pragma once

// 数据层聚合根。
//
// main 只调 init / start / stop。具体建哪种 PLC、串口参数从哪来、
// 采集线程什么时候起，全部收在这里，main 不需要知道。
//
// PLC 实现沿用主项目的封装，全部收在 fx_plc/ 下：
//   fx_plc/fx_plc_instance       海天，三菱 FX 串口协议
//   fx_plc/tcp_plc_instance      驿公里，Modbus TCP
// 两者都实现 fx_plc/iplc_instance.h 里的 IPlcInstance；
// 实例由 fx_plc/fx_plc_instance_set.h 的 FxPlcInstanceSet 统一登记与持有。

#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/idevice_service.h"

#include <memory>
#include <string>

class DeviceService;

class DataLayer {
public:
    DataLayer();
    ~DataLayer();

    DataLayer(const DataLayer&) = delete;
    DataLayer& operator=(const DataLayer&) = delete;

    /// 读配置、建 PLC 实例、装配网关与设备服务。不启动线程。
    bool init();

    /// 启动采集线程
    void start();

    /// 停止采集线程（幂等）
    void stop();

    /// 业务层通过它访问设备
    IDeviceService& device();

    /// 本机型实际在用的 PLC 标识（云端同名回执/日志对齐用）
    const std::string& plcId() const { return plcId_; }

private:
    FxPlcInstanceSet fxPlcSet_;   // 主项目的 PLC 集合封装：addPlc / getPlc / getAll
    std::unique_ptr<DeviceService> device_;
    std::string plcId_;
    bool plcRegistered_{false};
};
