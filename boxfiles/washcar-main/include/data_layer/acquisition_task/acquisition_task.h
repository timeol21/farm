#ifndef ACQUISITION_TASK_H
#define ACQUISITION_TASK_H

#include <chrono>
#include <vector>
#include "data_layer/device/device_data.h"
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
        int _type;
        int _sampleIntervalSec;
        std::chrono::steady_clock::time_point _nextAcquisitionTime; //下次采集时间
};

#endif