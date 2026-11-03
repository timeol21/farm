#pragma once

//#include "data_layer/acquisition_task/serial_direct_device_acquisition_task.h"
//#include "data_layer/acquisition_task/plc_device_acquisition_task.h"
//#include "data_layer/acquisition_task/gpio_device_acquisition_task.h"
#include <vector>
#include <memory>
#include <string>
#include "common/config/config_object.h"
#include "data_layer/acquisition_task/acquisition_task.h"
#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "data_layer/acquisition_task/tcp_plc_device_acquisition_task.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"

// 装配采集任务:按洗车机点位表声明的协议选实现(海天 FX 串口 / 驿公里 Modbus TCP),
// 把这台洗车机的软元件表交给采集任务 —— 采集层只管"按表采"。
class DeviceAcquisitionTask {

    public:
        DeviceAcquisitionTask(
            //const SerialDirectDeviceAcquisitionTask& serialDirectDeviceTask,
                              //const PlcDeviceAcquisitionTask& plcDeviceAcquisitionTask,

                             FxPlcInstanceSet& fxPlcSet,
                              //const GPIODeviceAcquisitionTask& gpioDeviceAcquisitionTask
                             const washcar::cfg::MachinePointTable& pointTable
                            );
        ~DeviceAcquisitionTask();

        const std::vector< std::unique_ptr<AcquisitionTask> > getTasks() const ;

    private:
        FxPlcInstanceSet& fxPlcSet_;
        // 这台洗车机的软元件表(采哪些点、哪些点打变化日志),也决定了用哪种协议
        const washcar::cfg::MachinePointTable& pointTable_;
        //SerialDirectDeviceAcquisitionTask serialDirectDeviceTask_;
        //PlcDeviceAcquisitionTask plcDeviceTask_;
        //GPIODeviceAcquisitionTask gpioDeviceTask_;
};
