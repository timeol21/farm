#pragma once

//#include "data_layer/acquisition_task/serial_direct_device_acquisition_task.h"
//#include "data_layer/acquisition_task/plc_device_acquisition_task.h"
//#include "data_layer/acquisition_task/gpio_device_acquisition_task.h"
#include <vector>
#include <memory>
#include "data_layer/acquisition_task/acquisition_task.h"   
#include "data_layer/fx_plc/fx_plc_instance_set.h"
class DeviceAcquisitionTask {

    public:
        DeviceAcquisitionTask(
            //const SerialDirectDeviceAcquisitionTask& serialDirectDeviceTask,
                              //const PlcDeviceAcquisitionTask& plcDeviceAcquisitionTask,
                             
                             FxPlcInstanceSet& fxPlcSet
                              //const GPIODeviceAcquisitionTask& gpioDeviceAcquisitionTask
                            );
        ~DeviceAcquisitionTask();

        const std::vector< std::unique_ptr<AcquisitionTask> > getTasks() const ;

    private:
        FxPlcInstanceSet& fxPlcSet_;
        //SerialDirectDeviceAcquisitionTask serialDirectDeviceTask_;
        //PlcDeviceAcquisitionTask plcDeviceTask_;
        //GPIODeviceAcquisitionTask gpioDeviceTask_;
};