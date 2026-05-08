#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <limits>
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// 全局配置（从JSON读取）
json globalConfig;
json doorLockConfig;  // 门锁GPIO配置

// 动态生成路径（从配置读取pin）
string getGpioSysPath() {
    return "/sys/class/gpio/gpio" + to_string(doorLockConfig["pin"].get<int>());
}
string getGpioValuePath() {
    return getGpioSysPath() + "/value";
}

// 函数声明
bool isRoot();                              // 检查root权限
bool gpioExists();                          // 检查GPIO是否已导出
bool exportGPIO();                          // 导出GPIO
bool setGPIOdirection(const string& dir);   // 设置GPIO方向
bool setGPIOValue(int value);               // 设置GPIO电平
int getGPIOValue();                         // 获取当前GPIO电平
void printGpioInfo();                       // 打印GPIO状态
void interactiveControl();                   // 交互式控制逻辑
bool unexportGPIO();                        // 释放GPIO
void InitEncoding();                        // 编码初始化（统一风格）

int main() {
    // 1. 初始化编码
    InitEncoding();

    // 2. 读取JSON配置文件
    ifstream f("config.json");
    if(!f.is_open()) {
        cout<<"config.json 打开失败"<<endl;
        return 1;
    }
    f>>globalConfig;

    // 3. 查找门锁GPIO设备配置
    string devId="door_lock_1";
    bool found=false;
    for(auto &dev:globalConfig["linux_direct_devices"]["gpio_devices"]) {
        if(dev["id"] == devId) {
            doorLockConfig = dev;
            found = true;
            break;
        }
    }
    if(!found) {
        cout<<"未找到门锁设备（"<<devId<<"）"<<endl;
        return 1;
    }

    // 自动计算物理编号（验证配置正确性）
    int gpioGroup = doorLockConfig["gpio_group"].get<int>();
    int gpioPinNum = doorLockConfig["gpio_pin_num"].get<int>();
    int calculatedPin = gpioGroup * 32 + gpioPinNum;
    int configPin = doorLockConfig["pin"].get<int>();

    // 打印设备信息（和烟感程序一致）
    cout<<"================================ 设备配置信息 ================================"<<endl;
    cout<<"设备: "<<doorLockConfig["name"]<<endl;
    cout<<"GPIO组号: "<<gpioGroup<<endl;
    cout<<"GPIO引脚号: "<<gpioPinNum<<endl;
    cout<<"物理编号计算公式: 组号 × 32 + 引脚号"<<endl;
    cout<<"自动计算值: "<<calculatedPin<<endl;
    cout<<"配置文件值: "<<configPin<<endl;
    if(calculatedPin != configPin) {
        cout<<"⚠️  警告：计算值与配置值不一致，将使用配置值！"<<endl;
    }
    cout<<"GPIO芯片: "<<doorLockConfig["chip_name"]<<endl;
    cout<<"有效电平: "<<doorLockConfig["active_logic"]<<endl;
    cout<<"描述: "<<doorLockConfig["description"]<<endl;
    cout<<"=============================================================================="<<endl;

    // 4. 检查root权限
    if (!isRoot()) {
        cerr << "错误：必须使用root权限运行（请加sudo）" << endl;
        return 1;
    }

    // 5. 自动导出GPIO（已导出则跳过）
    if (gpioExists()) {
        cout << "信息：GPIO" << configPin << "已导出，直接使用" << endl;
    } else {
        cout << "信息：正在导出GPIO" << configPin << "..." << endl;
        if (!exportGPIO()) {
            cerr << "错误：GPIO导出失败" << endl;
            return 1;
        }
        cout << "成功：GPIO导出完成" << endl;
        usleep(200000);  // 等待sysfs节点初始化（200ms）
    }

    // 6. 设置为输出模式（从JSON读取默认方向）
    string defaultDir = doorLockConfig["direction"].get<string>();
    cout << "信息：正在设置GPIO为" << defaultDir << "模式..." << endl;
    if (!setGPIOdirection(defaultDir)) {
        cerr << "错误：设置GPIO方向失败" << endl;
        return 1;
    }

    // 7. 初始状态设置（从JSON读取初始电平）
    int initialValue = doorLockConfig["initial_value"].get<int>();
    if (setGPIOValue(initialValue)) {
        cout << "成功：初始状态设置为" << (initialValue==1?"3.3V高电平（继电器导通）":"0V低电平（继电器断开）") << endl;
    } else {
        cerr << "错误：初始状态设置失败" << endl;
        return 1;
    }

    // 8. 打印初始状态
    cout << "\n=== GPIO初始化完成 ===" << endl;
    printGpioInfo();

    // 9. 进入交互式控制模式
    cout << "\n=== 交互式控制模式 ===" << endl;
    cout << "操作说明：" << endl;
    cout << "  输入 1 → GPIO输出3.3V（继电器导通，门锁解锁）" << endl;
    cout << "  输入 0 → GPIO输出0V（继电器断开，门锁上锁）" << endl;
    cout << "  输入 q → 退出程序（自动恢复初始状态）" << endl;
    cout << "======================" << endl;
    interactiveControl();

    // 10. 程序退出时释放GPIO并恢复初始状态
    cout << "\n信息：正在释放GPIO并恢复初始状态..." << endl;
    setGPIOValue(initialValue);  // 恢复初始电平
    unexportGPIO();

    cout << "程序已退出" << endl;
    return 0;
}

/**
 * 编码初始化（统一风格）
 */
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

/**
 * 检查是否为root权限
 */
bool isRoot() {
    return geteuid() == 0;
}

/**
 * 检查GPIO是否已导出
 */
bool gpioExists() {
    struct stat st;
    return stat(getGpioSysPath().c_str(), &st) == 0;
}

/**
 * 导出GPIO
 */
bool exportGPIO() {
    ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open()) {
        cerr << "错误：无法打开export文件（权限不足或系统不支持sysfs）" << endl;
        return false;
    }
    exportFile << doorLockConfig["pin"].get<int>();
    exportFile.close();
    return true;
}

/**
 * 设置GPIO方向（in/out）
 */
bool setGPIOdirection(const string& dir) {
    if (dir != "in" && dir != "out") {
        cerr << "错误：方向参数必须是\"in\"或\"out\"" << endl;
        return false;
    }

    string dirPath = getGpioSysPath() + "/direction";
    ofstream dirFile(dirPath);
    if (!dirFile.is_open()) {
        cerr << "错误：无法打开direction文件（" << dirPath << "）" << endl;
        return false;
    }
    dirFile << dir;
    dirFile.close();
    return true;
}

/**
 * 设置GPIO输出电平
 * @param value 0（低电平）或 1（高电平）
 */
bool setGPIOValue(int value) {
    if (value != 0 && value != 1) {
        cerr << "错误：电平值必须是0或1" << endl;
        return false;
    }

    ofstream valueFile(getGpioValuePath());
    if (!valueFile.is_open()) {
        cerr << "错误：无法打开value文件（权限不足）" << endl;
        return false;
    }
    valueFile << value;
    valueFile.close();
    return true;
}

/**
 * 获取当前GPIO电平
 * @return 0/1（失败返回-1）
 */
int getGPIOValue() {
    ifstream valueFile(getGpioValuePath());
    if (!valueFile.is_open()) {
        cerr << "错误：无法读取value文件" << endl;
        return -1;
    }
    string valueStr;
    valueFile >> valueStr;
    valueFile.close();
    return stoi(valueStr);
}

/**
 * 打印GPIO当前状态
 */
void printGpioInfo() {
    cout << "  主板接口：IO4（GPIO" << doorLockConfig["pin"] << "）" << endl;
    cout << "  Sysfs编号：" << doorLockConfig["pin"] << endl;
    cout << "  电压域：" << doorLockConfig["voltage_domain"] << endl;
    cout << "  当前状态：" << (getGPIOValue() == 1 ? "3.3V高电平（继电器导通）" : "0V低电平（继电器断开）") << endl;
    cout << "  接线规范：GPIO → 1KΩ限流电阻 → 继电器控制端 → 主板GND" << endl;
}

/**
 * 交互式控制逻辑（持续接收输入）
 */
void interactiveControl() {
    string input;
    while (true) {
        cout << "\n请输入控制指令（0/1/q）：";
        cin >> input;

        // 处理输入
        if (input == "1") {
            if (setGPIOValue(1)) {
                cout << "✅ 已设置为：3.3V高电平（继电器导通，门锁解锁）" << endl;
            }
        } else if (input == "0") {
            if (setGPIOValue(0)) {
                cout << "✅ 已设置为：0V低电平（继电器断开，门锁上锁）" << endl;
            }
        } else if (input == "q" || input == "Q") {
            cout << "⚠️  正在退出控制模式..." << endl;
            break;
        } else {
            cout << "❌ 无效指令！请输入0（上锁）、1（解锁）或q（退出）" << endl;
        }

        // 清空输入缓冲区
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

/**
 * 释放GPIO（程序退出时调用）
 */
bool unexportGPIO() {
    ofstream unexportFile("/sys/class/gpio/unexport");
    if (!unexportFile.is_open()) {
        cerr << "错误：无法释放GPIO（权限不足）" << endl;
        return false;
    }
    unexportFile << doorLockConfig["pin"].get<int>();
    unexportFile.close();
    return true;
}