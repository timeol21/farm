#include "business_layer/device/device_acquisition_task.h"
#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "data_layer/acquisition_task/fx_fault_acquisition_task.h"

//#include <iostream>
// DeviceAcquisitionTask::DeviceAcquisitionTask(//const SerialDirectDeviceAcquisitionTask& serialDirectDeviceTask,
//                                              //const PlcDeviceAcquisitionTask& plcDeviceAcquisitionTask,
//                                              //const GPIODeviceAcquisitionTask& gpioDeviceAcquisitionTask)
//                                              : 
//                                              //serialDirectDeviceTask_(serialDirectDeviceTask),
//                                                //plcDeviceTask_(plcDeviceAcquisitionTask), 
//                                                //gpioDeviceTask_(gpioDeviceAcquisitionTask
//                                                fxPlcSet_(fxPlcSet)
//                                             ){
   
//     //配置各种设备的采集任务    
// }
DeviceAcquisitionTask::DeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet)
    : fxPlcSet_(fxPlcSet) {}
DeviceAcquisitionTask::~DeviceAcquisitionTask() {

}

const std::vector< std::unique_ptr<AcquisitionTask> >  DeviceAcquisitionTask::getTasks() const {
    std::vector<std::unique_ptr<AcquisitionTask> > tasks;
    //tasks.reserve(20);
    //tasks.push_back(std::make_unique<TempHumidSensorAcquisitionTask> ( serialDirectDeviceTask_.getTempHumidSensorTask()) );
    //tasks.push_back(std::make_unique<DoorLockAcquisitionTask> (gpioDeviceTask_.getDoorLockTask()) );
    //tasks.push_back(std::make_unique<SolenoidAcquisitionTask> (plcDeviceTask_.getSolenoidAcquisitionTask()) );
    //tasks.push_back(std::make_unique<InfraredSensorAcquisitionTask> (plcDeviceTask_.getInfraredSensorTask())) ;
    //tasks.push_back(std::make_unique<WaterLevelSensorAcquisitionTask> (plcDeviceTask_.getWaterLevelSensorTask())); 
    //tasks.push_back(std::make_unique<SmokeDetectorAcquisitionTask> (plcDeviceTask_.getSmokeDetectorTask()));
    tasks.push_back(std::make_unique<FxDeviceAcquisitionTask>(fxPlcSet_));
    //std::cout << "[DEBUG] getTasks() size = " << tasks.size() << std::endl;
    tasks.push_back(std::make_unique<FxFaultAcquisitionTask>(fxPlcSet_));
    return tasks;
}