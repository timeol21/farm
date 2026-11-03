#include "data_layer/acquisition_task/acquisition_task.h"
#include "common/config/point.h"
#include "common/log/log_manager.h"

#include <string>
#include <unordered_map>

AcquisitionTask::AcquisitionTask(int type, int sampleIntervalSec)
    : _type(type), _sampleIntervalSec(sampleIntervalSec)
{
    _nextAcquisitionTime = std::chrono::steady_clock::now();
}

AcquisitionTask::~AcquisitionTask() {

}

bool AcquisitionTask::isAcquisitionData()
{
    return true;
}

int AcquisitionTask::getType() const {
    return _type;
}

int AcquisitionTask::getSampleIntervalSec() const {
    return _sampleIntervalSec;
}

std::chrono::steady_clock::time_point AcquisitionTask::getNextAcquisitionTime() const {
    return _nextAcquisitionTime;
}

void AcquisitionTask::logReadFailure(const std::string& plcId, const std::string& point) {
    // 每 60 次(采集周期 500ms,约 30 秒)打一条,避免采集循环刷屏。
    // 计数放函数内 static: 采集任务对象每轮采集都是新造的,成员变量保不住跨轮计数。
    static std::unordered_map<std::string, int> failCounters;
    const std::string key = plcId + ":" + point;
    int& count = failCounters[key];
    ++count;
    if (count == 1 || count % 60 == 0) {
        LOG_WARNING(std::string("[AcquisitionTask] read failed, plcId=") + plcId
            + ", point=" + point + ", count=" + std::to_string(count));
    }
}