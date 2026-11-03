// #include "data_layer/acquisition_task/door_lock_acquisition_task.h"

// DoorLockAcquisitionTask::DoorLockAcquisitionTask(int type, int sampleIntervalSec)
//                             : AcquisitionTask(type,sampleIntervalSec){

// }

// bool DoorLockAcquisitionTask::isAcquisitionData() {
//     auto now = std::chrono::steady_clock::now();

//     if(now >=_nextAcquisitionTime ) {
//         _nextAcquisitionTime = now + std::chrono::seconds(_sampleIntervalSec);
//         return true;
//     }
//     return false;
// }