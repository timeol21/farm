#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cstddef> 
#include <chrono>
#include <thread>
#include <iomanip>
#include <locale.h>  
#include <string>
#include <stdio.h> 
#include "Init.cpp"
using namespace std;

int main() {
    int mainChoice;
    Init init("/dev/ttyS4","/sys/class/gpio/gpio33/value");
    init.InitEncoding();
    init.InitSerial();    //初始化串口
    if (!init.ExportGPIOX()) {  // 调用对象的ExportGPIOX方法并判断
        cout << "GPIO初始化失败，程序无法正常运行传感器检测功能" << endl;
        init.CloseSerial();  // 失败时先关闭已初始化的串口，避免资源泄漏
        return -1;           // 退出程序，返回错误码-1
    }
    bool OpenSolenoidValve();
    bool CloseSolenoidValve();
    void DetectSensorStatus();
    void PrintMainMenu();
    init.CloseSerial();
    return 0;
}

/*
连接方式：
通过485通信和PLC连接，控制电池阀（现在是这个）
通过GPIO控制
*/


/*
水位，烟感，电池阀，报警器，门锁，红外感应，温湿度
程序的中文显示初始化
串口的配置：
串口初始化
电池阀开启
电池阀关闭
关闭串口


GPIO输入不需要初始化串口
*/