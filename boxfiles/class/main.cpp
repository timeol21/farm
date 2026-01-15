#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iomanip>
#include <string>
using namespace std;
#include "Init.cpp"

int main() {
    Init init("/dev/ttyS4","/sys/class/gpio/gpio33/value");
    init.InitEncoding();
    init.InitSerial("/dev/ttyS4");
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