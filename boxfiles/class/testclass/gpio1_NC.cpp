#include <iostream>              //使用于接在GPIO1上的NC的传感器
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>

// -------------------------- GPIO 配置与计算逻辑 --------------------------
// 核心公式：瑞芯微 RK3568 GPIO 物理编号 = 组号 × 32 + 引脚号
// 示例说明（当前使用 GPIO1 脚）：
// - 硬件定义：GPIO1_A1_u（来自规格书 6.1.7 章节）
// - 组号：GPIO1 → 组号=1；引脚号：A1 → 引脚号=1
// - 计算：1 × 32 + 1 = 33 → 物理编号=33
// 其他 GPIO 口适配示例：
// - GPIO2（IO2）：硬件定义 GPIO3_A5_d → 组号=3，引脚号=5 → 3×32+5=101
// - GPIO3（IO3）：硬件定义 GPIO1_A0_u → 组号=1，引脚号=0 → 1×32+0=32
// - GPIO4（IO4）：硬件定义 GPIO3_A4_d → 组号=3，引脚号=4 → 3×32+4=100
// - GPIO5（IO5）：硬件定义 GPIO3_A3_d → 组号=3，引脚号=3 → 3×32+3=99
// ------------------------------------------------------------------------
const int GPIO_GROUP = 1;    // GPIO 组号（根据硬件定义修改）
const int GPIO_PIN = 1;      // GPIO 引脚号（根据硬件定义修改）
const std::string GPIO_SENSOR = std::to_string(GPIO_GROUP * 32 + GPIO_PIN);  // 自动计算物理编号
const std::string GPIO_PATH = "/sys/class/gpio/";

// 信号处理函数：捕获 Ctrl+C，释放 GPIO 后退出
void signalHandler(int signum) {
    std::cout << "\n收到退出信号，释放 GPIO 资源（物理编号：" << GPIO_SENSOR << "）..." << std::endl;
    // 释放当前使用的 GPIO
    std::ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << GPIO_SENSOR;
        unexportFile.close();
    }
    exit(signum);
}

// 导出 GPIO（若未导出）
void exportGPIO(const std::string& gpioNum) {
    std::ifstream gpioDir(GPIO_PATH + "gpio" + gpioNum);
    if (!gpioDir.good()) {  // 若 GPIO 未导出，执行导出
        std::ofstream exportFile(GPIO_PATH + "export");
        if (!exportFile.is_open()) {
            throw std::runtime_error("导出 GPIO" + gpioNum + "失败：请用 sudo 运行程序");
        }
        exportFile << gpioNum;
        exportFile.close();
        usleep(100000);  // 等待系统初始化（100ms）
    }
    gpioDir.close();
}

// 设置 GPIO 为输入模式
void setGPIOAsInput(const std::string& gpioNum) {
    std::ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
    if (!dirFile.is_open()) {
        throw std::runtime_error("设置 GPIO" + gpioNum + "为输入模式失败");
    }
    dirFile << "in";
    dirFile.close();
}

// 读取 GPIO 输入电平（1=高电平，0=低电平）
int readGPIOValue(const std::string& gpioNum) {
    std::ifstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw std::runtime_error("读取 GPIO" + gpioNum + "电平失败");
    }
    int value;
    valueFile >> value;
    valueFile.close();
    return value;
}

int main() {
    // 注册信号处理函数（捕获 Ctrl+C）
    signal(SIGINT, signalHandler);

    try {
        // 打印 GPIO 配置信息（方便核对）
        std::cout << "================================ GPIO 配置信息 ================================" << std::endl;
        std::cout << "GPIO 组号：" << GPIO_GROUP << std::endl;
        std::cout << "GPIO 引脚号：" << GPIO_PIN << std::endl;
        std::cout << "物理编号计算公式：组号 × 32 + 引脚号" << std::endl;
        std::cout << "当前 GPIO 物理编号：" << GPIO_SENSOR << std::endl;
        std::cout << "================================================================================" << std::endl;

        // 初始化传感器对应的 GPIO（仅输入模式）
        std::cout << "初始化传感器检测 GPIO 接口..." << std::endl;
        exportGPIO(GPIO_SENSOR);
        setGPIOAsInput(GPIO_SENSOR);

        // 关键：明确常闭传感器规则（与常开相反）
        std::cout << "初始化完成！传感器已独立供电（常闭规则：高电平=导通，低电平=不导通）" << std::endl;
        std::cout << "按 Ctrl+C 退出程序" << std::endl;
        std::cout << "------------------------------------------------------------------------" << std::endl;
        std::cout << "| GPIO物理编号 | GPIO电平       | 传感器状态 |" << std::endl;
        std::cout << "------------------------------------------------------------------------" << std::endl;

        // 循环检测传感器状态（仅读取，无其他控制逻辑）
        while (true) {
            int gpioLevel = readGPIOValue(GPIO_SENSOR);  // 读取原始 GPIO 电平
            std::string sensorState;

            // 常闭传感器核心逻辑：高电平=导通，低电平=不导通（与常开完全相反）
            sensorState = (gpioLevel == 1) ? "导通" : "不导通";

            // 格式化输出，清晰展示对应关系
            std::cout << "| " << GPIO_SENSOR << "            | " << (gpioLevel == 1 ? "高电平(1)" : "低电平(0)") 
                      << "           | " << (sensorState == "导通" ? "导通    " : "不导通  ") 
                      << "   |" << std::endl;

            sleep(1);  // 每秒更新一次检测状态
        }

    } catch (const std::exception& e) {
        std::cerr << "\n错误：" << e.what() << std::endl;
        // 异常退出时释放 GPIO
        signalHandler(1);
        return 1;
    }

    return 0;
}

/*
g++ gpio1_NC.cpp -o gpio1_NC -std=c++11
*/