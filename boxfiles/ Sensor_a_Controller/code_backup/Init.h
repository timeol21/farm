#ifndef INIT_H
#define INIT_H
#include <string> 
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
using namespace std;
class Init{
private:
    int SerialPortStutas;  //SerialPortStutas串口文件描述符
    string Gpio_Value_Path;   //gpio_value_path
    string PortName;
    string Gpio_Export_Path;
    string Gpio_Dir_Path;
    bool ConfigureSerial(int fd);   //串口参数配置函数
public:
    void InitEncoding();  //初始化编译设置，使中文可以正常编译
    Init(const std::string& portname, const std::string& gpio_value_path);          //用于所有的端口号，端口文件的状态，GPIO路径的初始化
    ~Init();
    bool InitSerial();    //串口初始化函数ss
    bool ExportGPIOX();
    void CloseSerial();   //关闭串口

};   //分号容易漏
#endif

// #ifndef Init_H
// #define Init_H

// #include <iostream>
// #include <cstdio>
// #include <clocale>
// #include <termios.h>
// #include <fcntl.h>
// #include <unistd.h>
// #include <string>

// class SerialManager {
// private:
//     // 私有成员变量（封装串口资源）
//     int SerialPortStutas;                // 串口文件描述符（初始化为-1）
//     std::string gpioValuePath;   // 传感器GPIO路径
//     std::string portName;        // 串口端口名

//     // 私有方法：串口参数配置（外部无需调用）
//     bool ConfigureSerial();

// public:
//     // 构造函数（支持自定义串口端口、GPIO路径，默认值与原逻辑一致）
//     SerialManager(const std::string& port = "/dev/ttyS4", 
//                   const std::string& gpioPath = "/sys/class/gpio/gpio33/value");
    
//     // 析构函数（自动关闭串口，释放资源）
//     ~SerialManager();

//     // 公有方法：编码初始化（解决中文乱码）
//     void InitEncoding();

//     // 公有方法：串口初始化入口（打开串口 + 配置参数）
//     bool InitSerial();

//     // 公有方法：主动关闭串口
//     void CloseSerial();

//     // 公有接口：获取串口文件描述符（外部读写串口时需要）
//     int getSerialFd() const;

//     // 公有接口：设置/获取GPIO路径（增加灵活性）
//     void setGpioValuePath(const std::string& path);
//     std::string getGpioValuePath() const;

//     // 公有接口：设置/获取串口端口名（支持动态切换串口）
//     void setPortName(const std::string& port);
//     std::string getPortName() const;
// };

// #endif