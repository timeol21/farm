#include "business_layer/device/device_acquisition_task.h"
#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "data_layer/acquisition_task/tcp_plc_device_acquisition_task.h"

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
DeviceAcquisitionTask::DeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet,
                                               const washcar::cfg::MachinePointTable& pointTable)
    : fxPlcSet_(fxPlcSet), pointTable_(pointTable) {}
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

    if (pointTable_.protocol == "modbusTcp") {
        tasks.push_back(std::make_unique<TcpPlcDeviceAcquisitionTask>(fxPlcSet_, pointTable_));
    } else {
        // haiTian(默认)
        tasks.push_back(std::make_unique<FxDeviceAcquisitionTask>(fxPlcSet_, pointTable_));
    }
    // 原来海天挂两个任务(FxDeviceAcquisitionTask + FxFaultAcquisitionTask),
    // 驿公里挂两个(TcpPlcDeviceAcquisitionTask + TcpPlcFaultAcquisitionTask)。
    // 点位已经合并进洗车机点位表,由同一个任务一次采完 —— 见 default_point.h。
    return tasks;
}
