#pragma once

// 业务层看到的"设备"，只有点位。
//
// 业务层不认识 PLC 协议、不认识采集线程、不 include 任何 data_layer 的实现头，
// 只认这个接口。换采集方式、加设备类型，业务层都不用改。
//
// ============ 读 = 读内存，不是读 PLC ============
// 下面所有"读"方法返回的都是**采集线程采回内存的那份快照**，实现里不会发起任何
// 串口 / Modbus 请求。PLC 的唯一读者是采集线程。
// 所以调用方要预期：拿到的值最多滞后一个采集周期（acquisition.intervalMs）。

#include "common/plc_point.h"

#include <string>
#include <utility>
#include <vector>

class IDeviceService {
public:
    virtual ~IDeviceService() = default;

    /// 全量点位快照，**来自内存缓存**，不触发 PLC 读。
    /// 缓存还没有数据时返回空表（调用方应视为"暂无设备数据"）。
    virtual PlcPointValues viewAllDeviceStatus() = 0;

    /// M / X 置位。plcId 用配置里那台的名字
    virtual bool forceFxM(const std::string& plcId, int addr, bool turnOn) = 0;
    virtual bool forceFxX(const std::string& plcId, int addr, bool turnOn) = 0;

    /// 取"变过的 PLC"：每项 = {plcId, 当前全量点位}
    virtual std::vector<std::pair<std::string, PlcPointValues>> getChanged() = 0;

    /// 清除变化标志（上报成功后调用）
    virtual void clearChanged(const std::vector<std::string>& plcIds) = 0;
};
