#include "data_layer/acquisition_task/acquisition_task.h"

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