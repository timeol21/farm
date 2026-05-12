#ifndef SOLENOID_ACQUISITION_TASK_H
#define SOLENOID_ACQUISITION_TASK_H 

#include "data_layer/acquisition_task/acquisition_task.h"

class SolenoidAcquisitionTask : public AcquisitionTask {
    public:
        SolenoidAcquisitionTask(int type, int sampleIntervalSec);
        ~SolenoidAcquisitionTask() override;

        bool isAcquisitionData() override;

};

#endif