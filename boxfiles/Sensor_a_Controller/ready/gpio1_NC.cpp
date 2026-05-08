#include <iostream>             //用于控制通用设备（继电器、电磁阀等类门锁原理设备），测试过，可用
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>
#include <cstdlib>
#include <limits>  // 用于清空输入缓冲区

// ------------ 适配DC-A568-V06：通用GPIO输出控制（适配IO1-IO5） ------------
// 硬件定义规则：瑞芯微 RK3568 GPIO 物理编号 = 组号 × 32 + 引脚号
// 不同IO口配置示例（修改以下2个常量即可切换）：
// - IO1（GPIO1_A1_u）：GROUP=1, PIN=1 → 物理编号=33
// - IO2（GPIO3_A5_d）：GROUP=3, PIN=5 → 物理编号=101
// - IO3（GPIO1_A0_u）：GROUP=1, PIN=0 → 物理编号=32
// - IO4（GPIO3_A4_d）：GROUP=3, PIN=4 → 物理编号=100
// - IO5（GPIO3_A3_d）：GROUP=3, PIN=3 → 物理编号=99
const int GPIO_GROUP = 3;
const int GPIO_PIN = 5;
const std::string GPIO_DEVICE = std::to_string(GPIO_GROUP * 32 + GPIO_PIN);
const std::string GPIO_PATH = "/sys/class/gpio/";
bool isRunning = true;

// 信号处理：Ctrl+C释放GPIO并复位为低电平
void signalHandler(int signum) {
    std::cout << "\n[退出] 释放GPIO" << GPIO_DEVICE << "资源，复位为低电平..." << std::endl;
    // 复位GPIO为低电平（设备关闭）
    std::ofstream valueFile(GPIO_PATH + "gpio" + GPIO_DEVICE + "/value");
    if (valueFile.is_open()) {
        valueFile << 0;
        valueFile.close();
    }
    // 释放GPIO资源
    std::ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << GPIO_DEVICE;
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

// 设置GPIO为输出模式
void setGPIOAsOutput(const std::string& gpioNum) {
    std::ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
    if (!dirFile.is_open()) {
        throw std::runtime_error("设置GPIO" + gpioNum + "为输出模式失败，检查物理编号是否正确！");
    }
    dirFile << "out";
    dirFile.close();
    usleep(100000);
}

// 设置GPIO输出电平（0=低电平/设备关，1=高电平/设备开）
void setGPIOValue(const std::string& gpioNum, int value) {
    if (value != 0 && value != 1) {
        throw std::runtime_error("GPIO电平值必须为0或1！");
    }
    std::ofstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw std::runtime_error("设置GPIO" + gpioNum + "电平失败，接线是否正确？");
    }
    valueFile << value;
    valueFile.close();
}

// 读取GPIO当前电平（用于状态显示）
int readGPIOValue(const std::string& gpioNum) {
    int val1, val2;
    std::ifstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw std::runtime_error("读取GPIO" + gpioNum + "电平失败！");
    }
    valueFile >> val1;
    valueFile.close();
    usleep(10000); // 10ms防抖间隔
    std::ifstream valueFile2(GPIO_PATH + "gpio" + gpioNum + "/value");
    valueFile2 >> val2;
    valueFile2.close();
    return (val1 == val2) ? val1 : val1; // 防抖校验
}

// 交互式控制逻辑（接收用户输入0/1/q）
void interactiveControl() {
    std::string input;
    while (isRunning) {
        std::cout << "\n请输入控制指令（0=设备关/1=设备开/q=退出）：";
        std::cin >> input;

        // 处理输入指令
        if (input == "1") {
            setGPIOValue(GPIO_DEVICE, 1);
            std::cout << "✅ 已设置GPIO" << GPIO_DEVICE << "为高电平(1) → 设备开启" << std::endl;
        } else if (input == "0") {
            setGPIOValue(GPIO_DEVICE, 0);
            std::cout << "✅ 已设置GPIO" << GPIO_DEVICE << "为低电平(0) → 设备关闭" << std::endl;
        } else if (input == "q" || input == "Q") {
            std::cout << "⚠️  接收到退出指令，正在清理资源..." << std::endl;
            break;
        } else {
            std::cout << "❌ 无效指令！仅支持输入0（关）、1（开）、q（退出）" << std::endl;
        }

        // 清空输入缓冲区，避免无效输入导致死循环
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        
        // 打印当前GPIO状态
        int currentLevel = readGPIOValue(GPIO_DEVICE);
        std::string levelStr = (currentLevel == 1) ? "高电平(1)" : "低电平(0)";
        std::string deviceState = (currentLevel == 1) ? "开启" : "关闭";
        std::cout << "-----------------------------------------------------------------------" << std::endl;
        std::cout << "| GPIO物理号 | 实际电平 | 设备状态 |" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;
        std::cout << "| " << GPIO_DEVICE << "        | " << levelStr << " | " << deviceState << "     |" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;
    }
}

int main() {
    signal(SIGINT, signalHandler); // 注册退出信号（Ctrl+C）
    try {
        // 打印主板GPIO配置信息（方便核对）
        std::cout << "==================== DC-A568-V06 GPIO通用设备控制程序 ====================" << std::endl;
        std::cout << "GPIO硬件定义：GPIO" << GPIO_GROUP << "_A" << GPIO_PIN << (GPIO_GROUP==1 ? "_u" : "_d") << std::endl;
        std::cout << "主板物理编号：" << GPIO_GROUP << "×32+" << GPIO_PIN << "=" << GPIO_DEVICE << std::endl;
        std::cout << "适配设备：通用类门锁原理设备（继电器、电磁阀等）" << std::endl;
        std::cout << "电平逻辑：高电平(1)=设备开，低电平(0)=设备关" << std::endl;
        std::cout << "========================================================================" << std::endl;

        // 初始化GPIO（导出+设置为输出模式）
        std::cout << "[初始化] 配置GPIO" << GPIO_DEVICE << "为输出模式..." << std::endl;
        exportGPIO(GPIO_DEVICE);
        setGPIOAsOutput(GPIO_DEVICE);
        
        // 初始状态强制设为低电平（设备关闭）
        setGPIOValue(GPIO_DEVICE, 0);
        std::cout << "[初始化完成] GPIO初始状态：低电平(0) → 设备关闭" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;
        std::cout << "| GPIO物理号 | 实际电平 | 设备状态 |" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;
        std::cout << "| " << GPIO_DEVICE << "        | 低电平(0) | 关闭     |" << std::endl;
        std::cout << "-----------------------------------------------------------------------" << std::endl;

        // 进入交互式控制模式
        std::cout << "\n=== 进入交互式控制模式（按Ctrl+C或输入q退出）===" << std::endl;
        interactiveControl();

        // 退出前清理资源
        signalHandler(0);

    } catch (const std::exception& e) {
        std::cerr << "\n[错误] " << e.what() << std::endl;
        signalHandler(1);
        return 1;
    }
    return 0;
}

