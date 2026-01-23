#ifndef DEVICE_H
#define DEVICE_H

#include <string>
#include <iostream>
using namespace std;

class Device {
public:
    string id;        // 对应你的 id
    string type;      // 对应你的 type (类型名)
    string portPath;  // 对应你的串口路径

    Device();
    virtual ~Device();

    // 对应你习惯的 setBaseInfo 风格
    void setBaseInfo(string newId, string newType, string newPort);

    // 纯虚函数：这是第一版架构要求的，强制子类实现各自的工作逻辑
    virtual void execute() = 0; 
};

#endif