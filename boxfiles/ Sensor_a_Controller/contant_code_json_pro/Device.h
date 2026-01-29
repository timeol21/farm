#ifndef DEVICE_H
#define DEVICE_H

#include <string>
#include <iostream>

class Device {
protected:
    std::string id;
    std::string type;
    std::string name;
    std::string portPath;

public:
    // 工业级初始化列表，避免二次赋值
    Device(const std::string& _id, const std::string& _type, 
           const std::string& _name, const std::string& _port)
        : id(_id), type(_type), name(_name), portPath(_port) {}

    // 虚析构函数：确保子类内存能被正确释放
    virtual ~Device() = default;

    // 纯虚函数：所有设备必须实现自己的执行逻辑
    virtual void execute() = 0;
};

#endif