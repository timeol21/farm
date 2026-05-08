#include <iostream>        //适用于接继电器输出端为NO的电池阀（接在GPIO2上）
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>


/*
GPIO口高电平1，低电平0
GPIO1 默认是上拉（高电平1）
GPIO2 默认是下拉（低电平0）
GPIO3 默认是上拉（高电平1）
GPIO4 默认是下拉（低电平0）
GPIO5 默认是下拉（低电平0）

继电器输入端，如果COM和X有电压差，则为对应的输入端灯亮，打开继电器，如果COM和X没有电压差，则为对应的输入端灯灭，关闭继电器
继电器打开时，
输出端为NC的设备为打开（电磁阀所在的回路，通），输出端为NO的设备为关闭
继电器关闭时，
输出端为NC的设备为关闭，输出端为NO的设备为打开（回路通）
*/

// -------------------------- GPIO 配置与计算逻辑 --------------------------
// 核心公式：瑞芯微 RK3568 GPIO 物理编号 = 组号 × 32 + 引脚号
// 所选 GPIO2（IO2）：硬件定义 GPIO3_A5_d → 组号=3，引脚号=5 → 3×32+5=101
// 若需更换其他输出口（GPIO4/5），仅修改以下组号和引脚号即可：
// - GPIO4（IO4）：组号=3，引脚号=4 → 3×32+4=100
// - GPIO5（IO5）：组号=3，引脚号=3 → 3×32+3=99
// ------------------------------------------------------------------------
const int GPIO_GROUP = 3;    // GPIO 组号（GPIO2对应组号=3）
const int GPIO_PIN = 5;      // GPIO 引脚号（GPIO2对应引脚号=5）
const std::string GPIO_CONTROL = std::to_string(GPIO_GROUP * 32 + GPIO_PIN);  // 自动计算物理编号（101）
const std::string GPIO_PATH = "/sys/class/gpio/";

// 信号处理函数：捕获 Ctrl+C，释放 GPIO 后退出
void signalHandler(int signum) {
    std::cout << "\n收到退出信号，释放 GPIO 资源（物理编号：" << GPIO_CONTROL << "）..." << std::endl;
    // 退出时将 GPIO 设为低电平（关闭控制器）
    std::ofstream valueFile(GPIO_PATH + "gpio" + GPIO_CONTROL + "/value");
    if (valueFile.is_open()) {
        valueFile << "0";
        valueFile.close();
    }
    // 释放 GPIO
    std::ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << GPIO_CONTROL;
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

// 设置 GPIO 为输出模式
void setGPIOAsOutput(const std::string& gpioNum) {
    std::ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
    if (!dirFile.is_open()) {
        throw std::runtime_error("设置 GPIO" + gpioNum + "为输出模式失败");
    }
    dirFile << "out";
    dirFile.close();
}

// 设置 GPIO 输出电压（1=3.3V，0=0V）
void setGPIOVoltage(const std::string& gpioNum, int voltageFlag) {
    if (voltageFlag != 0 && voltageFlag != 1) {
        throw std::invalid_argument("电压设置错误：仅支持 0（0V）或 1（3.3V）");
    }
    std::ofstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw std::runtime_error("设置 GPIO" + gpioNum + "电压失败");
    }
    valueFile << voltageFlag;
    valueFile.close();
    // 打印当前状态
    std::string voltage = (voltageFlag == 1) ? "3.3V（高电平）（电池阀开）" : "0V（低电平）（电磁阀关）";
    std::cout << "已设置 GPIO" << gpioNum << " 输出：" << voltage << std::endl;
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
        std::cout << "当前控制 GPIO 物理编号：" << GPIO_CONTROL << std::endl;
        std::cout << "电压输出规则：1=3.3V（高电平），0=0V（低电平）" << std::endl;
        std::cout << "================================================================================" << std::endl;

        // 初始化 GPIO（导出 + 设为输出模式）
        std::cout << "初始化 GPIO 输出接口..." << std::endl;
        exportGPIO(GPIO_CONTROL);
        setGPIOAsOutput(GPIO_CONTROL);
        // 初始状态设为 0V（关闭控制器）
        setGPIOVoltage(GPIO_CONTROL, 0);

        std::cout << "\n初始化完成！输入 1 输出 3.3V，输入 0 输出 0V，输入 q 退出程序" << std::endl;

        // 交互控制：接收用户输入设置电压
        char input;
        while (true) {
            std::cout << "\n请输入指令（0/1/q）：";
            std::cin >> input;

            if (input == '1') {
                setGPIOVoltage(GPIO_CONTROL, 1);  // 输出 3.3V
            } else if (input == '0') {
                setGPIOVoltage(GPIO_CONTROL, 0);  // 输出 0V
            } else if (input == 'q' || input == 'Q') {
                std::cout << "正在退出程序，关闭 GPIO 输出..." << std::endl;
                setGPIOVoltage(GPIO_CONTROL, 0);  // 退出前设为 0V
                signalHandler(0);  // 释放 GPIO 资源
            } else {
                std::cout << "无效指令！仅支持输入 0（0V）、1（3.3V）或 q（退出）" << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "\n错误：" << e.what() << std::endl;
        // 异常退出时设为 0V 并释放 GPIO
        setGPIOVoltage(GPIO_CONTROL, 0);
        signalHandler(1);
        return 1;
    }

    return 0;
}

/*
g++ gpio2_NO.cpp -o gpio2_NO -std=c++11
*/