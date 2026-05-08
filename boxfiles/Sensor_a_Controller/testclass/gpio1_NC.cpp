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

// #include <iostream>              //使用于接在GPIO1上的NC的传感器，（门锁，电磁阀）
// #include <fstream>
// #include <string>
// #include <unistd.h>
// #include <stdexcept>
// #include <signal.h>

// // -------------------------- GPIO 配置与计算逻辑 --------------------------
// // 核心公式：瑞芯微 RK3568 GPIO 物理编号 = 组号 × 32 + 引脚号
// // 示例说明（当前使用 GPIO1 脚）：
// // - 硬件定义：GPIO1_A1_u（来自规格书 6.1.7 章节）
// // - 组号：GPIO1 → 组号=1；引脚号：A1 → 引脚号=1
// // - 计算：1 × 32 + 1 = 33 → 物理编号=33
// // 其他 GPIO 口适配示例：
// // - GPIO2（IO2）：硬件定义 GPIO3_A5_d → 组号=3，引脚号=5 → 3×32+5=101
// // - GPIO3（IO3）：硬件定义 GPIO1_A0_u → 组号=1，引脚号=0 → 1×32+0=32
// // - GPIO4（IO4）：硬件定义 GPIO3_A4_d → 组号=3，引脚号=4 → 3×32+4=100
// // - GPIO5（IO5）：硬件定义 GPIO3_A3_d → 组号=3，引脚号=3 → 3×32+3=99
// // ------------------------------------------------------------------------
// const int GPIO_GROUP = 1;    // GPIO 组号（根据硬件定义修改）
// const int GPIO_PIN = 1;      // GPIO 引脚号（根据硬件定义修改）
// const std::string GPIO_SENSOR = std::to_string(GPIO_GROUP * 32 + GPIO_PIN);  // 自动计算物理编号
// const std::string GPIO_PATH = "/sys/class/gpio/";

// // 信号处理函数：捕获 Ctrl+C，释放 GPIO 后退出
// void signalHandler(int signum) {
//     std::cout << "\n收到退出信号，释放 GPIO 资源（物理编号：" << GPIO_SENSOR << "）..." << std::endl;
//     // 释放当前使用的 GPIO
//     std::ofstream unexportFile(GPIO_PATH + "unexport");
//     if (unexportFile.is_open()) {
//         unexportFile << GPIO_SENSOR;
//         unexportFile.close();
//     }
//     exit(signum);
// }

// // 导出 GPIO（若未导出）
// void exportGPIO(const std::string& gpioNum) {
//     std::ifstream gpioDir(GPIO_PATH + "gpio" + gpioNum);
//     if (!gpioDir.good()) {  // 若 GPIO 未导出，执行导出
//         std::ofstream exportFile(GPIO_PATH + "export");
//         if (!exportFile.is_open()) {
//             throw std::runtime_error("导出 GPIO" + gpioNum + "失败：请用 sudo 运行程序");
//         }
//         exportFile << gpioNum;
//         exportFile.close();
//         usleep(100000);  // 等待系统初始化（100ms）
//     }
//     gpioDir.close();
// }

// // 设置 GPIO 为输入模式
// void setGPIOAsInput(const std::string& gpioNum) {
//     std::ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
//     if (!dirFile.is_open()) {
//         throw std::runtime_error("设置 GPIO" + gpioNum + "为输入模式失败");
//     }
//     dirFile << "in";
//     dirFile.close();
// }

// // 读取 GPIO 输入电平（1=高电平，0=低电平）
// int readGPIOValue(const std::string& gpioNum) {
//     std::ifstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
//     if (!valueFile.is_open()) {
//         throw std::runtime_error("读取 GPIO" + gpioNum + "电平失败");
//     }
//     int value;
//     valueFile >> value;
//     valueFile.close();
//     return value;
// }

// int main() {
//     // 注册信号处理函数（捕获 Ctrl+C）
//     signal(SIGINT, signalHandler);

//     try {
//         // 打印 GPIO 配置信息（方便核对）
//         std::cout << "================================ GPIO 配置信息 ================================" << std::endl;
//         std::cout << "GPIO 组号：" << GPIO_GROUP << std::endl;
//         std::cout << "GPIO 引脚号：" << GPIO_PIN << std::endl;
//         std::cout << "物理编号计算公式：组号 × 32 + 引脚号" << std::endl;
//         std::cout << "当前 GPIO 物理编号：" << GPIO_SENSOR << std::endl;
//         std::cout << "================================================================================" << std::endl;

//         // 初始化传感器对应的 GPIO（仅输入模式）
//         std::cout << "初始化传感器检测 GPIO 接口..." << std::endl;
//         exportGPIO(GPIO_SENSOR);
//         setGPIOAsInput(GPIO_SENSOR);

//         // 关键：明确常闭传感器规则（与常开相反）
//         std::cout << "初始化完成！传感器已独立供电（常闭规则：高电平=导通，低电平=不导通）" << std::endl;
//         std::cout << "按 Ctrl+C 退出程序" << std::endl;
//         std::cout << "------------------------------------------------------------------------" << std::endl;
//         std::cout << "| GPIO物理编号 | GPIO电平       | 传感器状态 |" << std::endl;
//         std::cout << "------------------------------------------------------------------------" << std::endl;

//         // 循环检测传感器状态（仅读取，无其他控制逻辑）
//         while (true) {
//             int gpioLevel = readGPIOValue(GPIO_SENSOR);  // 读取原始 GPIO 电平
//             std::string sensorState;

//             // 常闭传感器核心逻辑：高电平=导通，低电平=不导通（与常开完全相反）
//             sensorState = (gpioLevel == 1) ? "导通" : "不导通";

//             // 格式化输出，清晰展示对应关系
//             std::cout << "| " << GPIO_SENSOR << "            | " << (gpioLevel == 1 ? "高电平(1)" : "低电平(0)") 
//                       << "           | " << (sensorState == "导通" ? "导通    " : "不导通  ") 
//                       << "   |" << std::endl;

//             sleep(1);  // 每秒更新一次检测状态
//         }

//     } catch (const std::exception& e) {
//         std::cerr << "\n错误：" << e.what() << std::endl;
//         // 异常退出时释放 GPIO
//         signalHandler(1);
//         return 1;
//     }

//     return 0;
// }

// /*
// g++ gpio1_NC.cpp -o gpio1_NC -std=c++11
// */