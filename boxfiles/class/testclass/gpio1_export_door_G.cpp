#include <iostream>                //门锁的一个接GND,一个接gpio的实现代码
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits>  // 用于清空输入缓冲区

// DC-A568-V06主板GPIO4配置（规格书6.1.7章节）
// GPIO4（IO4）硬件定义：GPIO3_A4_d → 组号=3，引脚号=4 → 物理编号=3×32+4=100
const std::string GPIO_CHIP = "gpiochip0";  // RK3568默认GPIO芯片
const int GPIO4_PIN = 100;                  // IO4对应sysfs编号
const std::string GPIO_SYS_PATH = "/sys/class/gpio/gpio" + std::to_string(GPIO4_PIN);
const std::string GPIO_VALUE_PATH = GPIO_SYS_PATH + "/value";

// 函数声明
bool isRoot();                              // 检查root权限
bool gpioExists();                          // 检查GPIO是否已导出
bool exportGPIO();                          // 导出GPIO4
bool setGPIOdirection(const std::string& dir = "out");  // 设置输出模式
bool setGPIOValue(int value);               // 设置GPIO电平（1=3.3V，0=0V）
int getGPIOValue();                         // 获取当前GPIO电平
void printGpioInfo();                       // 打印GPIO状态
void interactiveControl();                   // 交互式控制逻辑
bool unexportGPIO();                        // 释放GPIO（程序退出时调用）

int main() {
    // 1. 检查root权限
    if (!isRoot()) {
        std::cerr << "错误：必须使用root权限运行（请加sudo）" << std::endl;
        return 1;
    }

    // 2. 自动导出GPIO4（已导出则跳过）
    if (gpioExists()) {
        std::cout << "信息：GPIO4（编号" << GPIO4_PIN << "）已导出，直接使用" << std::endl;
    } else {
        std::cout << "信息：正在导出GPIO4（编号" << GPIO4_PIN << "）..." << std::endl;
        if (!exportGPIO()) {
            std::cerr << "错误：GPIO4导出失败" << std::endl;
            return 1;
        }
        std::cout << "成功：GPIO4导出完成" << std::endl;
        usleep(200000);  // 等待sysfs节点初始化（200ms）
    }

    // 3. 设置为输出模式
    std::cout << "信息：正在设置GPIO4为输出模式..." << std::endl;
    if (!setGPIOdirection("out")) {
        std::cerr << "错误：设置GPIO4方向失败" << std::endl;
        return 1;
    }

    // 4. 初始状态强制设为0V（确保上电继电器不通电）
    if (setGPIOValue(0)) {
        std::cout << "成功：初始状态设置为0V（继电器断开）" << std::endl;
    } else {
        std::cerr << "错误：初始状态设置失败" << std::endl;
        return 1;
    }

    // 5. 打印初始状态
    std::cout << "\n=== GPIO4初始化完成 ===" << std::endl;
    printGpioInfo();

    // 6. 进入交互式控制模式
    std::cout << "\n=== 交互式控制模式 ===" << std::endl;
    std::cout << "操作说明：" << std::endl;
    std::cout << "  输入 1 → GPIO4输出3.3V（继电器导通，控制端与GND形成电压差）" << std::endl;
    std::cout << "  输入 0 → GPIO4输出0V（继电器断开，控制端与GND无电压差）" << std::endl;
    std::cout << "  输入 q → 退出程序（自动恢复0V断开状态）" << std::endl;
    std::cout << "======================" << std::endl;
    interactiveControl();

    // 7. 程序退出时释放GPIO并恢复0V
    std::cout << "\n信息：正在释放GPIO4并恢复断开状态..." << std::endl;
    setGPIOValue(0);  // 强制断开继电器
    unexportGPIO();

    std::cout << "程序已退出" << std::endl;
    return 0;
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
    return stat(GPIO_SYS_PATH.c_str(), &st) == 0;
}

/**
 * 导出GPIO4
 */
bool exportGPIO() {
    std::ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open()) {
        std::cerr << "错误：无法打开export文件（权限不足或系统不支持sysfs）" << std::endl;
        return false;
    }
    exportFile << GPIO4_PIN;
    exportFile.close();
    return true;
}

/**
 * 设置GPIO方向
 */
bool setGPIOdirection(const std::string& dir) {
    if (dir != "in" && dir != "out") {
        std::cerr << "错误：方向参数必须是\"in\"或\"out\"" << std::endl;
        return false;
    }

    std::string dirPath = GPIO_SYS_PATH + "/direction";
    std::ofstream dirFile(dirPath);
    if (!dirFile.is_open()) {
        std::cerr << "错误：无法打开direction文件（" << dirPath << "）" << std::endl;
        return false;
    }
    dirFile << dir;
    dirFile.close();
    return true;
}

/**
 * 设置GPIO输出电平
 * @param value 0（低电平，断开）或 1（高电平，导通）
 */
bool setGPIOValue(int value) {
    if (value != 0 && value != 1) {
        std::cerr << "错误：电平值必须是0或1" << std::endl;
        return false;
    }

    std::ofstream valueFile(GPIO_VALUE_PATH);
    if (!valueFile.is_open()) {
        std::cerr << "错误：无法打开value文件（权限不足）" << std::endl;
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
    std::ifstream valueFile(GPIO_VALUE_PATH);
    if (!valueFile.is_open()) {
        std::cerr << "错误：无法读取value文件" << std::endl;
        return -1;
    }
    std::string valueStr;
    valueFile >> valueStr;
    valueFile.close();
    return std::stoi(valueStr);
}

/**
 * 打印GPIO当前状态
 */
void printGpioInfo() {
    std::cout << "  主板接口：IO4（GPIO3_A4_d）" << std::endl;
    std::cout << "  Sysfs编号：" << GPIO4_PIN << std::endl;
    std::cout << "  电压域：3.3V" << std::endl;
    std::cout << "  当前状态：" << (getGPIOValue() == 1 ? "3.3V高电平（继电器导通）" : "0V低电平（继电器断开）") << std::endl;
    std::cout << "  接线规范：GPIO4 → 1KΩ限流电阻 → 继电器控制端 → 主板GND" << std::endl;
}

/**
 * 交互式控制逻辑（持续接收输入）
 */
void interactiveControl() {
    std::string input;
    while (true) {
        std::cout << "\n请输入控制指令（0/1/q）：";
        std::cin >> input;

        // 处理输入
        if (input == "1") {
            if (setGPIOValue(1)) {
                std::cout << "✅ 已设置为：3.3V高电平（继电器导通，控制端与GND形成电压差）" << std::endl;
            }
        } else if (input == "0") {
            if (setGPIOValue(0)) {
                std::cout << "✅ 已设置为：0V低电平（继电器断开，控制端与GND无电压差）" << std::endl;
            }
        } else if (input == "q" || input == "Q") {
            std::cout << "⚠️  正在退出控制模式..." << std::endl;
            break;
        } else {
            std::cout << "❌ 无效指令！请输入0（断开）、1（导通）或q（退出）" << std::endl;
        }

        // 清空输入缓冲区（避免无效输入导致死循环）
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

/**
 * 释放GPIO（程序退出时调用）
 */
bool unexportGPIO() {
    std::ofstream unexportFile("/sys/class/gpio/unexport");
    if (!unexportFile.is_open()) {
        std::cerr << "错误：无法释放GPIO（权限不足）" << std::endl;
        return false;
    }
    unexportFile << GPIO4_PIN;
    unexportFile.close();
    return true;
}

/*
g++ -std=c++11 gpio1_export_door_G.cpp -o gpio1_export_door_G -std=c++11
*/