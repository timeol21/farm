#ifndef CAMERA_ACQUISITION_TASK_H
#define CAMERA_ACQUISITION_TASK_H

#include "data_layer/acquisition_task/acquisition_task.h"

class CameraAcquisitionTask : public AcquisitionTask {
    public:
        CameraAcquisitionTask(int type, int sampleIntervalSec);
        ~CameraAcquisitionTask() override;

        bool isAcquisitionData() override;

};

#endif