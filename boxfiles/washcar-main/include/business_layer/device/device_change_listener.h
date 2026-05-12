#pragma once

#include <string>

// 点位变化描述
struct ChangeRecord {
    std::string deviceId;       
    std::string pointName;       // 例M100
    std::string oldValue;        // 旧状态 "0"/"1" 或数值字符串
    std::string newValue;        // 新状态
    int deviceType;              // 7=FX PLC
};

// 监听器接口
class IDeviceChangeListener {
public:
    virtual ~IDeviceChangeListener() = default;
    // 当单个点位变化时回调
    virtual void onDeviceChanged(const ChangeRecord& change) = 0;
};