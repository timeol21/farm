#ifndef INIT_H
#define INIT_H

#include <string>
#include <map>
#include <vector>
using namespace std;
struct PLCInfo;

// 串口管理类
class InitSerial{
private:
    string portPath;          // 单串口路径
    int SerialPortStatus;     // 单串口fd
    // 全局串口映射，仅本类管理
    static map<string, int> serialFdMap;  // 键：串口路径  值：fd
    static map<int, int> plcSerialMap;    // 键：plc_id   值：fd

    bool ConfigureSerial(int fd); // 串口底层配置，通用逻辑
    void CloseSerial();           // 单串口关闭，

public:
    //空参（批量初始化）
    InitSerial() : SerialPortStatus(-1) {}
    //带参（单串口初始化）
    InitSerial(string port) : portPath(port), SerialPortStatus(-1) {}  
    //释放单串口资源
    ~InitSerial() { CloseSerial(); }

    // 公有方法：单串口初始化、PLC上电检测
    bool initSerial();          // 单串口初始化
    bool CheckPLCOnline();      // PLC上电检测

    // 静态公有方法：多串口全局管理核心（外部主函数/业务类仅调用此组接口）
    static bool InitAllSerial(const vector<PLCInfo>& plcList); // 批量初始化所有串口
    static int  GetFdByPlcId(int plcId);                      // 通过plc获取fd的唯一接口
    static int  GetFdByPort(const string& port);               // 备用：通过串口路径获取fd
    static void CloseAllSerial();                              // 程序退出时统一关闭所有串口
};

#endif