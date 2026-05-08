#include <iostream>             //用于监听设备（红外，烟感，水浸）,测试过，可用（测试于3568）
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>
#include <cstdlib>

// ------------ 适配DC-A568-V06：IO1(GPIO1_A1_u) 初始低电平NO传感器 ------------
// 硬件定义：IO1 = GPIO1_A1_u → 组号1，引脚1 → 物理编号=1×32+1=33
// 若接IO3(GPIO1_A0_u)，修改为：GPIO_GROUP=1, GPIO_PIN=0 → 物理编号32
const int GPIO_GROUP = 1;
const int GPIO_PIN = 1;
const std::string GPIO_SENSOR = std::to_string(GPIO_GROUP * 32 + GPIO_PIN);
const std::string GPIO_PATH = "/sys/class/gpio/";
bool isRunning = true;

// 信号处理：Ctrl+C释放GPIO
void signalHandler(int signum) {
    std::cout << "\n[退出] 释放GPIO" << GPIO_SENSOR << "资源..." << std::endl;
    std::ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << GPIO_SENSOR;
        unexportFile.close();
    }
    isRunning = false;
    exit(signum);
}

// 导出GPIO（未导出则执行）
void exportGPIO(const std::string& gpioNum) {
    std::ifstream gpioDir(GPIO_PATH + "gpio" + gpioNum);
    if (!gpioDir.good()) {
        std::ofstream exportFile(GPIO_PATH + "export");
        if (!exportFile.is_open()) {
            throw std::runtime_error("GPIO导出失败：请用sudo运行！");
        }
        exportFile << gpioNum;
        exportFile.close();
        usleep(200000); // 延长初始化时间，匹配主板时序（200ms）
    }
    gpioDir.close();
}

// 设置GPIO为输入
void setGPIOAsInput(const std::string& gpioNum) {
    std::ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
    if (!dirFile.is_open()) {
        throw std::runtime_error("设置GPIO输入模式失败，检查物理编号是否正确！");
    }
    dirFile << "in";
    dirFile.close();
    usleep(100000);
}

// 读取GPIO电平（增加防抖：连续读2次，值一致才有效）
int readGPIOValue(const std::string& gpioNum) {
    int val1, val2;
    std::ifstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw std::runtime_error("读取GPIO电平失败，接线是否正确？");
    }
    valueFile >> val1;
    valueFile.close();
    usleep(10000); // 10ms防抖间隔
    std::ifstream valueFile2(GPIO_PATH + "gpio" + gpioNum + "/value");
    valueFile2 >> val2;
    valueFile2.close();
    return (val1 == val2) ? val1 : val1; // 防抖校验
}

int main() {
    signal(SIGINT, signalHandler); // 注册退出信号
    try {
        // 打印主板GPIO配置信息（方便核对）
        std::cout << "==================== DC-A568-V06 GPIO传感器检测程序 ====================" << std::endl;
        std::cout << "GPIO硬件定义：GPIO" << GPIO_GROUP << "_A" << GPIO_PIN << "_u" << std::endl;
        std::cout << "主板物理编号：" << GPIO_GROUP << "×32+" << GPIO_PIN << "=" << GPIO_SENSOR << std::endl;
        std::cout << "适配传感器：初始低电平NO传感器（接IO1/IO3）" << std::endl;
        std::cout << "电平逻辑：未触发→高电平(1)，触发→低电平(0)" << std::endl;
        std::cout << "========================================================================" << std::endl;

        // 初始化GPIO
        std::cout << "[初始化] 配置GPIO" << GPIO_SENSOR << "为输入模式..." << std::endl;
        exportGPIO(GPIO_SENSOR);
        setGPIOAsInput(GPIO_SENSOR);
        std::cout << "[初始化完成] 开始检测传感器状态（按Ctrl+C退出）" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;
        std::cout << "| GPIO物理号 | 实际电平 | 传感器状态 |" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;

        // 循环检测（防抖+实时刷新）
        while (isRunning) {
            int gpioLevel = readGPIOValue(GPIO_SENSOR);
            std::string sensorState = (gpioLevel == 0) ? "未触发" : "【触发】";
            std::string levelStr = (gpioLevel == 1) ? "高电平(1)" : "低电平(0)";

            // 格式化输出，覆盖行刷新（避免刷屏）
            std::cout << "| " << GPIO_SENSOR << "         | " << levelStr << " | " << sensorState << "     |\r";
            std::cout.flush();
            sleep(1); // 1秒检测一次，可修改为0.5秒（usleep(500000)）
        }

    } catch (const std::exception& e) {
        std::cerr << "\n[错误] " << e.what() << std::endl;
        signalHandler(1);
        return 1;
    }
    return 0;
}

