#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>
#include <limits>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// 全局配置
json globalConfig;
json sensorConfig;
string GPIO_PATH = "/sys/class/gpio/";

// 自动计算物理编号（组号×32+引脚号）
int calculateGpioPin(int group, int pinNum) {
    return group * 32 + pinNum;
}

// 信号处理函数：捕获Ctrl+C，释放GPIO
void signalHandler(int signum) {
    int gpioPin = sensorConfig["pin"].get<int>();
    cout << "\n收到退出信号，释放GPIO资源（物理编号：" << gpioPin << "）..." << endl;
    
    ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << gpioPin;
        unexportFile.close();
    }
    exit(signum);
}

// 导出GPIO（未导出则执行）
void exportGPIO(int gpioPin) {
    string gpioDirPath = GPIO_PATH + "gpio" + to_string(gpioPin);
    ifstream gpioDir(gpioDirPath);
    
    if (!gpioDir.good()) {
        ofstream exportFile(GPIO_PATH + "export");
        if (!exportFile.is_open()) {
            throw runtime_error("导出GPIO" + to_string(gpioPin) + "失败：请用sudo运行程序");
        }
        exportFile << gpioPin;
        exportFile.close();
        usleep(100000); // 等待100ms初始化
    }
    gpioDir.close();
}

// 设置GPIO为输入模式
void setGPIOAsInput(int gpioPin) {
    string dirPath = GPIO_PATH + "gpio" + to_string(gpioPin) + "/direction";
    ofstream dirFile(dirPath);
    
    if (!dirFile.is_open()) {
        throw runtime_error("设置GPIO" + to_string(gpioPin) + "为输入模式失败");
    }
    dirFile << "in";
    dirFile.close();
}

// 读取GPIO电平
int readGPIOValue(int gpioPin) {
    string valuePath = GPIO_PATH + "gpio" + to_string(gpioPin) + "/value";
    ifstream valueFile(valuePath);
    
    if (!valueFile.is_open()) {
        throw runtime_error("读取GPIO" + to_string(gpioPin) + "电平失败");
    }
    int value;
    valueFile >> value;
    valueFile.close();
    return value;
}

// 编码初始化（统一风格）
void InitEncoding() {
    const char* locales[] = {"zh_CN.UTF-8","en_US.UTF-8","C.UTF-8","POSIX"};
    for(auto loc:locales) {
        if(setlocale(LC_ALL,loc)!=NULL) {
            cout<<"编码初始化成功: "<<loc<<endl;
            return;
        }
    }
    perror("所有编码设置均失败，可能导致打印乱码");
}

int main() {
    // 1. 初始化编码
    InitEncoding();

    // 2. 读取JSON配置
    ifstream f("config.json");
    if(!f.is_open()) {
        cout<<"config.json 打开失败"<<endl;
        return 1;
    }
    f>>globalConfig;

    // 3. 查找常开传感器配置
    string devId = "sensor_no_1";
    bool found = false;
    for(auto &dev:globalConfig["linux_direct_devices"]["gpio_devices"]) {
        if(dev["id"] == devId) {
            sensorConfig = dev;
            found = true;
            break;
        }
    }
    if(!found) {
        cout<<"未找到常开传感器设备（"<<devId<<"）"<<endl;
        return 1;
    }

    // 4. 自动计算物理编号（验证配置中的pin是否正确）
    int gpioGroup = sensorConfig["gpio_group"].get<int>();
    int gpioPinNum = sensorConfig["gpio_pin_num"].get<int>();
    int calculatedPin = calculateGpioPin(gpioGroup, gpioPinNum);
    int configPin = sensorConfig["pin"].get<int>();
    
    // 打印配置信息（统一风格）
    cout << "================================ GPIO配置信息 ================================" << endl;
    cout << "设备名称：" << sensorConfig["name"] << endl;
    cout << "GPIO组号：" << gpioGroup << endl;
    cout << "GPIO引脚号：" << gpioPinNum << endl;
    cout << "物理编号计算公式：组号 × 32 + 引脚号" << endl;
    cout << "自动计算值：" << calculatedPin << endl;
    cout << "配置文件值：" << configPin << endl;
    if (calculatedPin != configPin) {
        cout << "⚠️  警告：计算值与配置值不一致，将使用配置值！" << endl;
    }
    cout << "传感器类型：" << sensorConfig["sensor_type"] << endl;
    cout << "电平规则：" << sensorConfig["level_rule"] << endl;
    cout << "================================================================================" << endl;

    // 5. 注册信号处理
    signal(SIGINT, signalHandler);

    try {
        // 6. 初始化GPIO
        cout << "初始化常开传感器检测GPIO接口..." << endl;
        exportGPIO(configPin);
        setGPIOAsInput(configPin);

        // 7. 打印初始化信息
        cout << "初始化完成！传感器已独立供电（" << sensorConfig["level_rule"] << "）" << endl;
        cout << "按Ctrl+C退出程序" << endl;
        cout << "------------------------------------------------------------------------" << endl;
        cout << "| GPIO物理编号 | GPIO电平       | 传感器状态 |" << endl;
        cout << "------------------------------------------------------------------------" << endl;

        // 8. 循环检测
        while (true) {
            int gpioLevel = readGPIOValue(configPin);
            string sensorState = (gpioLevel == 1) ? "不导通" : "导通";

            // 格式化输出
            cout << "| " << configPin << "            | " << (gpioLevel == 1 ? "高电平(1)" : "低电平(0)") 
                 << "           | " << (sensorState == "导通" ? "导通    " : "不导通  ") 
                 << "   |" << endl;

            sleep(1); // 每秒检测一次
        }

    } catch (const exception& e) {
        cerr << "\n错误：" << e.what() << endl;
        signalHandler(1);
        return 1;
    }

    return 0;
}