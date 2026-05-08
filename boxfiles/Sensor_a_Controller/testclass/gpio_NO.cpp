#include <iostream>             //测试过，可以用于直连监听设备（红外，烟感，水浸）可用（测试的时候是用rk3588）
#include <fstream>
#include <string>
#include <unistd.h>
#include <stdexcept>
#include <signal.h>
#include <cstdlib>

using namespace std;

// ====================== 你只需要修改这里 ======================
const string GPIO_SENSOR = "106";       // 直接填 GPIO 编号：96/107/106/62
const string GPIO_DIR     = "out";      // 方向：in 输入 / out 输出(需要根据电路的需求设置，信号电路是否需要gpio提供电压)
const int    INIT_VALUE   = 1;          // 输出模式初始值：0 / 1（输入模式无效）
// ==============================================================

const string GPIO_PATH = "/sys/class/gpio/";
bool isRunning = true;

// Ctrl+C 退出时释放 GPIO
void signalHandler(int signum) {
    cout << "\n[退出] 释放GPIO " << GPIO_SENSOR << " 资源..." << endl;
    ofstream unexportFile(GPIO_PATH + "unexport");
    if (unexportFile.is_open()) {
        unexportFile << GPIO_SENSOR;
        unexportFile.close();
    }
    isRunning = false;
    exit(signum);
}

// 自动导出 GPIO
void exportGPIO(const string& gpioNum) {
    ifstream gpioDir(GPIO_PATH + "gpio" + gpioNum);
    if (!gpioDir.good()) {
        ofstream exportFile(GPIO_PATH + "export");
        if (!exportFile.is_open()) {
            throw runtime_error("GPIO 导出失败：请用 sudo 运行！");
        }
        exportFile << gpioNum;
        exportFile.close();
        usleep(200000);
    }
}

// 设置方向：in / out
void setGPIODirection(const string& gpioNum, const string& dir) {
    ofstream dirFile(GPIO_PATH + "gpio" + gpioNum + "/direction");
    if (!dirFile.is_open()) {
        throw runtime_error("设置方向失败！");
    }
    dirFile << dir;
    dirFile.close();
    usleep(100000);
}

// 输出模式：设置初始值
void setGPIOOutputValue(const string& gpioNum, int value) {
    if (value != 0 && value != 1) {
        throw runtime_error("初始值只能是 0 或 1！");
    }

    ifstream dirCheck(GPIO_PATH + "gpio" + gpioNum + "/direction");
    string currentDir;
    dirCheck >> currentDir;
    dirCheck.close();

    if (currentDir == "in") {
        cerr << "⚠️  输入模式无法设置初始值，已跳过" << endl;
        return;
    }

    ofstream valueFile(GPIO_PATH + "gpio" + gpioNum + "/value");
    if (!valueFile.is_open()) {
        throw runtime_error("写入初始值失败！");
    }
    valueFile << value;
    valueFile.close();
    usleep(50000);
}

// 读取 GPIO（防抖）
int readGPIOValue(const string& gpioNum) {
    int v1, v2;
    ifstream val1File(GPIO_PATH + "gpio" + gpioNum + "/value");
    val1File >> v1;
    val1File.close();
    usleep(10000);

    ifstream val2File(GPIO_PATH + "gpio" + gpioNum + "/value");
    val2File >> v2;
    val2File.close();

    return (v1 == v2) ? v1 : v1;
}

int main() {
    signal(SIGINT, signalHandler);

    try {
        cout << "==================== RK3588 GPIO 控制程序 ====================" << endl;
        cout << "GPIO 编号:    " << GPIO_SENSOR << endl;
        cout << "方向模式:     " << GPIO_DIR << endl;
        cout << "输出初始值:   " << (GPIO_DIR == "out" ? to_string(INIT_VALUE) : "输入模式无效") << endl;
        cout << "==============================================================" << endl;

        // 1. 导出 GPIO
        exportGPIO(GPIO_SENSOR);

        // 2. 设置方向
        setGPIODirection(GPIO_SENSOR, GPIO_DIR);

        // 3. 输出模式 → 设置初始电平（你要的功能）
        if (GPIO_DIR == "out") {
            setGPIOOutputValue(GPIO_SENSOR, INIT_VALUE);
            cout << "[✅] 输出模式，已设置初始值: " << INIT_VALUE << endl;
        } else {
            cout << "[✅] 输入模式，无需初始值" << endl;
        }

        cout << "[初始化完成] 实时监控（按 Ctrl+C 退出）" << endl;
        cout << "------------------------------------------------------------" << endl;

        // 实时循环监听
        while (isRunning) {
            int level = readGPIOValue(GPIO_SENSOR);
            string levelStr = (level == 1) ? "高电平(1)" : "低电平(0)";
            string modeStr = (GPIO_DIR == "out") ? "输出" : "输入";

            cout << "GPIO " << GPIO_SENSOR << " | 模式:" << modeStr << " | 实时电平:" << levelStr << "\r";
            cout.flush();
            sleep(1);
        }

    } catch (const exception& e) {
        cerr << "\n[错误] " << e.what() << endl;
        signalHandler(1);
        return 1;
    }

    return 0;
}