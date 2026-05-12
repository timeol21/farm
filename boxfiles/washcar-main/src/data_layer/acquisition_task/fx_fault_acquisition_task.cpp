#include "data_layer/acquisition_task/fx_fault_acquisition_task.h"
#include "data_layer/fx_plc/fx_plc_status.h"
#include <iostream>

FxFaultAcquisitionTask::FxFaultAcquisitionTask(FxPlcInstanceSet& fxPlcSet)
    : AcquisitionTask(7, 1), fxPlcSet_(fxPlcSet) {}  // 每1秒采集

std::vector<DeviceData> FxFaultAcquisitionTask::execute() {
    std::vector<DeviceData> result;
    struct FaultPoint {
        char type;     // 'M' 或 'X'
        int address;   // M 用十进制，X 用八进制
    };
    std::vector<FaultPoint> faultPoints = {
        {'M', 3},  {'X', 3},
        {'M', 23}, {'X', 23},
        {'M', 26}, {'X', 26},
        {'M', 31}, {'X', 31},
        {'M', 32}, {'X', 32},
        {'M', 33}, {'X', 33},
        {'M', 34}, {'X', 34},
        {'M', 35}, {'X', 35},
        {'M', 36}, {'X', 36},
        {'M', 37}, {'X', 37},
        {'M', 40}, {'X', 40},
        {'M', 41}, {'X', 41},
        {'M', 42}, {'X', 42},
        {'M', 43}, {'X', 43},
        {'M', 220},
        {'M', 221},
        {'M', 223},
        {'M', 224},
        {'M', 225},
        {'M', 400},
        {'M', 47},
        {'M', 250},
        {'M', 251},
        {'M', 257},
        {'M', 259},
        {'M',381},
        {'S',0},{'X',44},{'X',50},{'M',1},{'M',187},
        {'M',500}
    };

    // 2. 遍历所有 PLC
    for (auto& [plcId, plc] : fxPlcSet_.getAll()) {
        if (!plc->connect()) {
            std::cerr << "[故障采集] PLC " << plcId << " 连接失败，跳过" << std::endl;
            continue;
        }

        // 创建只包含本次故障点位的状态对象
        FxPlcStatus status(plcId);

        for (const auto& fp : faultPoints) {
            bool state = false;
            bool readOk = false;

            if (fp.type == 'M') {
                readOk = plc->readM(fp.address, state);
                if (readOk) {
                    status.updateMBit(fp.address, state);
                }
            } else if (fp.type == 'X') {
                readOk = plc->readXBit(fp.address, state);
                if (readOk) {
                    status.updateXBit(fp.address, state);
                }
            }else if (fp.type == 'S') {              
                readOk = plc->readS(fp.address, state);
                if (readOk) status.updateSBit(fp.address, state);
            }

            if (!readOk) {
                std::cerr << "[故障采集] PLC " << plcId
                          << " 读取 " << fp.type << fp.address << " 失败" << std::endl;
            }
        }

        // 将部分状态包装为 DeviceData
        DeviceData data(7, status);
        result.push_back(data);
    }

    return result;
}