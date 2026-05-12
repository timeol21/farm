#include "data_layer/acquisition_task/camera_acquisition_task.h"

CameraAcquisitionTask::CameraAcquisitionTask(int type, int sampleIntervalSec)
    : AcquisitionTask(type, sampleIntervalSec)
{
    _nextAcquisitionTime = std::chrono::steady_clock::now();
}

CameraAcquisitionTask::~CameraAcquisitionTask()
{

}

bool CameraAcquisitionTask::isAcquisitionData()
{
    auto now = std::chrono::steady_clock::now();

    if( now >= _nextAcquisitionTime) {
        _nextAcquisitionTime = now + std::chrono::seconds(_sampleIntervalSec);
        return true;
    }
    return false;
}
