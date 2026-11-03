#ifndef ACQUISITION_TASK_H
#define ACQUISITION_TASK_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/config/config_object.h"
#include "data_layer/device/device_data.h"
#include "data_layer/fx_plc/fx_plc_status.h"
class AcquisitionTask {
    public:
        AcquisitionTask(int type, int sampleIntervalSec);
        virtual ~AcquisitionTask();

        virtual bool isAcquisitionData();
        //返回采集到的数据列表
        virtual std::vector<DeviceData> execute() = 0;

        int getType() const ;
        int getSampleIntervalSec() const ;
        //int getNextAcquisitionTime() const ;
        std::chrono::steady_clock::time_point getNextAcquisitionTime() const;

    protected:
        // 采集点位读失败告警。同一 PLC 的同一点位每 60 次(约 1 分钟)才打一条,
        // 避免采集循环刷屏。
        static void logReadFailure(const std::string& plcId, const std::string& point);

        // 关键点位变化日志:盯住点位表里的 keyStatePoints,值变了打一条。
        void logKeyStateChanges(const washcar::cfg::MachinePointTable& table,
                                const FxPlcStatus& status, const std::string& plcId) const;

        int _type;
        int _sampleIntervalSec;
        std::chrono::steady_clock::time_point _nextAcquisitionTime; //下次采集时间
};

#endif