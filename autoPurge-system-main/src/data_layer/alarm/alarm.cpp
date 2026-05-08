#include "data_layer/alarm/alarm_dao.h"

AlarmDao::AlarmDao(){

}

AlarmDao::~AlarmDao(){

}

// 新增告警
void AlarmDao::insertAlarm(const AlarmRecord& alarm){

}

// 查询当前未处理告警
std::vector<AlarmRecord> AlarmDao::getActiveAlarms(){

}

// 根据ID查询
std::optional<AlarmRecord> AlarmDao::getAlarmById(int id){

}

// 标记已处理
void AlarmDao::markAsResolved(int id){

}

// 清空（可选）
void AlarmDao::clearAll(){

}