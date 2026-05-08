#ifndef VALVECLASS_H
#define VALVECLASS_H

#include <string>
using namespace std;

// 电磁阀类：仅暴露电磁阀操作接口
class Valve{
private:
    int plcId;  // 关联的PLC编号
    // 内部获取串口fd
    int getSerialFd() const;

public:
    explicit Valve(int plcId) : plcId(plcId) {} // 仅传入plcId，关联串口
    bool OpenSolenoidValve();     // 开启电磁阀
    bool CloseSolenoidValve();    // 关闭电磁阀
    bool QuerySolenoidValveStatus();  // 查询电磁阀状态
};

#endif