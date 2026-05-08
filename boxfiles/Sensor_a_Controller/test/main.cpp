#include "PLCConfig.h"    
#include "Init.h"         
#include "Valve.h"   
#include <iostream>
#include <vector>
#include <thread>      
#include <chrono>      

using namespace std;
using namespace std::chrono_literals; 

int main() {
    cout << "=====================================" << endl;
    cout << "PLC多串口控制程序 - 极致低耦合版" << endl;
    cout << "串口仅初始化一次，全程复用fd" << endl;
    cout << "=====================================" << endl;

    // 步骤1：解析JSON配置
    vector<PLCInfo> plcList = PlcConfigParser::readPlcConfig("config.json");
    if (plcList.empty()) {
        cerr << "未读取到任何有效PLC配置，程序直接退出！" << endl;
        return -1;
    }

    // 步骤2：批量初始化所有串口
    if (!InitSerial::InitAllSerial(plcList)) {
        cerr << "部分串口初始化失败，继续执行可用设备..." << endl;
    }

    // 步骤3：遍历PLC执行业务
    for (const auto& plc : plcList) {
        cout << "\n-------------------------------------" << endl;
        cout << "开始执行业务：【PLC" << plc.plc_id << "】" << plc.description << endl;
        cout << "-------------------------------------" << endl;

        // 检查串口指标符号fd，判断串口有效性
        if (InitSerial::GetFdByPlcId(plc.plc_id) < 0) {
            cerr << "PLC" << plc.plc_id << " 无有效串口fd，跳过业务！" << endl;
            continue;
        }

        // PLC上电检测
        InitSerial plcSerial(plc.serial_port);
        plcSerial.initSerial(); 
        if (!plcSerial.CheckPLCOnline()) {
            cerr << "PLC" << plc.plc_id << " 未上电/离线，跳过电磁阀操作！" << endl;
            continue;
        }

        // 电磁阀操作
        Valve valve(plc.plc_id);
        valve.OpenSolenoidValve();
        valve.QuerySolenoidValveStatus();
        std::this_thread::sleep_for(2s);      
        valve.CloseSolenoidValve();

        cout << "PLC" << plc.plc_id << " 所有电磁阀业务执行完成！" << endl;
    }

    // 步骤4：程序退出，统一关闭所有串口
    InitSerial::CloseAllSerial();

    cout << "\n=====================================" << endl;
    cout << "所有PLC业务执行完毕，程序正常退出！" << endl;
    cout << "=====================================" << endl;

    return 0;
}