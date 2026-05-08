#pragma once

#include "data_layer/alarm/alarm_dao_object.h"
#include "common/config/config_load.h"
#include <vector>
#include <optional>
class IAlarmDao {
public:
    virtual ~IAlarmDao() = default;

    // 新增告警
    virtual void insertAlarm(const AlarmRecord& alarm) = 0;

    // 查询当前未处理告警
    virtual std::vector<AlarmRecord> getActiveAlarms() = 0;

    // 根据ID查询
    virtual std::optional<AlarmRecord> getAlarmById(int id) = 0;

    // 标记已处理
    virtual void markAsResolved(int id) = 0;

    // 清空（可选）
    virtual void clearAll() = 0;
};

class AlarmDao : public IAlarmDao{
public:
    AlarmDao();

    ~AlarmDao();

    // 新增告警
    void insertAlarm(const AlarmRecord& alarm);

    // 查询当前未处理告警
    std::vector<AlarmRecord> getActiveAlarms();

    // 根据ID查询
    std::optional<AlarmRecord> getAlarmById(int id);

    // 标记已处理
    void markAsResolved(int id);

    // 清空（可选）
    void clearAll();

private:





};