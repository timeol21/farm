#include "business_layer/device/device_acquisition_task.h"
#include "data_layer/acquisition_task/fx_device_acquisition_task.h"
#include "data_layer/acquisition_task/tcp_plc_device_acquisition_task.h"

DeviceAcquisitionTask::DeviceAcquisitionTask(FxPlcInstanceSet& fxPlcSet,
                                               const washcar::cfg::MachinePointTable& pointTable)
    : fxPlcSet_(fxPlcSet), pointTable_(pointTable) {}
DeviceAcquisitionTask::~DeviceAcquisitionTask() {

}

const std::vector< std::unique_ptr<AcquisitionTask> >  DeviceAcquisitionTask::getTasks() const {
    std::vector<std::unique_ptr<AcquisitionTask> > tasks;
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
