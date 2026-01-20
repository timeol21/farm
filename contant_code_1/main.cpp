#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>      // 新增：用于读取文件
#include "json.hpp"     // 新增：JSON库
#include "DeviceFactory.h"

using namespace std;
using json = nlohmann::json; // 简化别名

int main() {
    cout << "--- 智慧控制箱系统启动 (JSON配置版) ---" << endl;

    vector<Device*> devices;

    // --- 新增：从 JSON 文件加载配置 ---
    ifstream configFile("devices.json");
    if (!configFile.is_open()) {
        cout << "错误: 找不到 devices.json 配置文件！" << endl;
        return -1;
    }

    json configData;
    configFile >> configData; // 解析 JSON

    for (auto& item : configData) {
        string kind = item["kind"];
        string id = item["id"];
        string name = item["name"];
        string port = item["port"];
        
        // 如果是电磁阀，读取 addr，否则默认为 0
        int addr = item.contains("addr") ? (int)item["addr"] : 0;

        // 调用工厂创建设备
        Device* dev = DeviceFactory::createDevice(kind, id, name, port, addr);
        if (dev != nullptr) {
            devices.push_back(dev);
        }
    }
    // ----------------------------------

    cout << "系统初始化完毕，从 JSON 加载了 " << devices.size() << " 个设备。" << endl;

    while (true) {
        cout << "\n>> [状态轮询开始]" << endl;
        for (Device* d : devices) {
            d->execute(); 
            this_thread::sleep_for(chrono::milliseconds(200)); 
        }
        this_thread::sleep_for(chrono::seconds(1));
    }

    for (Device* d : devices) delete d;
    return 0;
}

/*
make run“编译 + 运行”
或者
编译：输入 make（它只会重新编译你改动过的那个文件，非常快）。

ztl@RK356X:~/program/contant_code_1$ make
g++ -std=c++11 -Wall -c main.cpp -o main.o
g++ -std=c++11 -Wall -c Device.cpp -o Device.o
g++ -std=c++11 -Wall -c SerialManager.cpp -o SerialManager.o
g++ -std=c++11 -Wall -c Sensors.cpp -o Sensors.o
g++ -std=c++11 -Wall -c Controllers.cpp -o Controllers.o
g++ -std=c++11 -Wall -c DeviceFactory.cpp -o DeviceFactory.o
g++ -std=c++11 -Wall main.o Device.o SerialManager.o Sensors.o Controllers.o DeviceFactory.o -o box_system -lpthread
ztl@RK356X:~/program/contant_code_1$ 

运行：输入 sudo ./box_system  //从最后 一行可以找到对应的程序

make clean //清除编译的.o文件
make
sudo ./box_system
*/