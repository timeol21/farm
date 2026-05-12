// #include "data_layer/acquisition_task/temp_humid_sensor_acquisition_task.h"

// TempHumidSensorAcquisitionTask::TempHumidSensorAcquisitionTask(int type, int sampleIntervalSec) 
//     : AcquisitionTask(type,sampleIntervalSec)
// {
// }


// bool TempHumidSensorAcquisitionTask::isAcquisitionData() {
//     auto now = std::chrono::steady_clock::now();
//     if(now >= _nextAcquisitionTime) {
//         _nextAcquisitionTime = now + std::chrono::seconds(_sampleIntervalSec);
//         return true;
//     }
//     return false;

// }