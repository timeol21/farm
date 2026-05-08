#ifndef SMOKE_ALARM_H
#define SMOKE_ALARM_H

#include "PLCComponentDevice.h"

class SmokeAlarm : public PLCComponentDevice {
public:
    SmokeAlarm(const std::string& deviceId = "smoke_alarm_1");
    ~SmokeAlarm() override = default;

    // 读取烟感数据（差异化逻辑）
    bool readData() override;
};

#endif // SMOKE_ALARM_H