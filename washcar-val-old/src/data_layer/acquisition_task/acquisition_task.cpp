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
    // 每 60 次(约 1 分钟)打一条,避免采集循环刷屏。
    static std::unordered_map<std::string, int> failCounters;
    const std::string key = plcId + ":" + point;
    int& count = failCounters[key];
    ++count;
    if (count == 1 || count % 60 == 0) {
        LOG_WARNING(std::string("[AcquisitionTask] read failed, plcId=") + plcId
            + ", point=" + point + ", count=" + std::to_string(count));
    }
}

void AcquisitionTask::logKeyStateChanges(const washcar::cfg::MachinePointTable& table,
                                          const FxPlcStatus& status,
                                          const std::string& plcId) const {
    // 每个 PLC 一份基准,第一轮只记基准不打日志
    static std::unordered_map<std::string, std::unordered_map<std::string, int>> lastValues;
    auto& perPlc = lastValues[plcId];

    for (const auto& name : table.keyStatePoints) {
        const washcar::cfg::SignalConfig* signal = nullptr;
        for (const auto& candidate : table.signals) {
            if (candidate.name == name) { signal = &candidate; break; }
        }
        if (!signal) continue;

        const auto parsed = washcar::cfg::parsePoint(signal->point);
        int current = 0;
        bool got = false;
        if (parsed.type == 'D') {
            uint16_t value = 0;
            got = status.getDRegister(parsed.addr, value);
            current = value;
        } else {
            bool value = false;
            if (parsed.type == 'M')      got = status.getMBit(parsed.addr, value);
            else if (parsed.type == 'X') got = status.getXBit(parsed.addr, value);
            else if (parsed.type == 'Y') got = status.getYBit(parsed.addr, value);
            else if (parsed.type == 'S') got = status.getSBit(parsed.addr, value);
            current = value ? 1 : 0;
        }
        if (!got) continue;

        auto entry = perPlc.find(signal->point);
        if (entry == perPlc.end()) {
            perPlc[signal->point] = current;
            continue;
        }
        if (entry->second != current) {
            LOG_INFO(std::string("[AcquisitionTask] PLC_KEY_STATE plcId=") + plcId
                + " point=" + signal->point + " val=" + std::to_string(current)
                + ", reason=state_change");
            entry->second = current;
        }
    }
}