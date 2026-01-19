#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>
#include <dirent.h>
#include <string>

using namespace std;

// -------------------------- 全局配置（核心：需替换为实际系统编号！） --------------------------
// 1. 从主板厂商获取 IO1/IO2 对应的 Linux 系统GPIO编号（示例用GPIO45/GPIO52，需根据实际修改）
const int IO1_SYS_GPIO = 45;    // IO1（硬件编码GPIO1_A1_u）对应的系统编号
const int IO2_SYS_GPIO = 52;    // IO2（硬件编码GPIO3_A5_d）对应的系统编号
// 2. GPIO文件路径（动态拼接系统编号）
string getGpioPath(int sysGpio, const string& type) {
    return "/sys/class/gpio/gpio" + to_string(sysGpio) + "/" + type;
}
const string GPIO_EXPORT_PATH = "/sys/class/gpio/export";


// -------------------------- 编码初始化（解决中文乱码，与contant_1.cpp一致） --------------------------
void InitEncoding() {
    const char* locales[] = {"zh_CN.UTF-8", "en_US.UTF-8", "C.UTF-8", "POSIX"};
    bool localeSet = false;

    for (const char* loc : locales) {
        if (setlocale(LC_ALL, loc) != NULL) {
            cout << "编码初始化成功：使用 " << loc << " 编码" << endl;
            localeSet = true;
            break;
        }
        cerr << "尝试设置 " << loc << " 编码失败，继续尝试下一个..." << endl;
    }

    if (!localeSet) {
        perror("所有编码设置均失败，可能导致打印乱码");
    }
}


// -------------------------- GPIO自动导出与配置（核心功能，适配IO1/IO2特性） --------------------------
/**
 * 自动导出GPIO并配置为输入模式
 * @param sysGpio：Linux系统GPIO编号
 * @param gpioName：GPIO名称（如"IO1（人体红外）"）
 * @param pullType：上拉/下拉（IO1=上拉，IO2=下拉）
 * @return 成功返回true，失败返回false
 */
bool ExportAndConfigGPIO(int sysGpio, const string& gpioName, const string& pullType) {
    cout << "\n=== 正在初始化 " << gpioName << " ===" << endl;

    // 1. 检查GPIO是否已导出（避免重复操作）
    string gpioDir = getGpioPath(sysGpio, "");
    DIR* dir = opendir(gpioDir.c_str());
    if (dir != NULL) {
        closedir(dir);
        cout << gpioName << "已导出，无需重复操作" << endl;
    } else {
        // 2. 导出GPIO（向export文件写入系统编号）
        ofstream exportFile(GPIO_EXPORT_PATH.c_str());
        if (!exportFile.is_open()) {
            cerr << gpioName << "导出失败：无权限（请用sudo运行程序）" << endl;
            return false;
        }
        exportFile << sysGpio;
        exportFile.close();

        // 等待100ms，确保系统生成GPIO目录（硬件操作需延迟）
        usleep(100000);
        dir = opendir(gpioDir.c_str());
        if (dir == NULL) {
            cerr << gpioName << "导出失败：系统编号错误（请核对主板映射表）" << endl;
            return false;
        }
        closedir(dir);
        cout << gpioName << "导出成功" << endl;
    }

    // 3. 配置GPIO为输入模式（检测传感器必须设为输入）
    ofstream dirFile(getGpioPath(sysGpio, "direction").c_str());
    if (!dirFile.is_open()) {
        cerr << gpioName << "配置输入模式失败：文件打开错误" << endl;
        return false;
    }
    dirFile << "in";
    dirFile.close();

    // 4. 提示上/下拉特性（匹配主板规格书，避免用户误判状态）
    cout << gpioName << "配置完成：输入模式，" << pullType << "（3.3V电平）" << endl;
    return true;
}


// -------------------------- GPIO状态读取（适配IO1/IO2上拉/下拉特性） --------------------------
/**
 * 读取单个GPIO的电平状态，并解析传感器状态
 * @param sysGpio：Linux系统GPIO编号
 * @param gpioName：GPIO名称（如"IO1（人体红外）"）
 * @param isPullUp：是否为上拉（IO1=true，IO2=false）
 * @return 成功返回true，失败返回false
 */
bool ReadGpioStatus(int sysGpio, const string& gpioName, bool isPullUp) {
    // 打开GPIO电平文件
    ifstream gpioFile(getGpioPath(sysGpio, "value").c_str());
    if (!gpioFile.is_open()) {
        cerr << gpioName << "读取失败：文件路径错误或无权限" << endl;
        return false;
    }

    // 读取电平值（0=低电平，1=高电平）
    int value;
    gpioFile >> value;
    gpioFile.close();

    // 结合上/下拉特性解析传感器状态（严格匹配主板规格书）
    cout << "\n[" << gpioName << "状态]";
    if (isPullUp) {
        // IO1：上拉输入（未触发=高电平1，触发=低电平0）
        if (value == 0) {
            cout << " 触发（低电平）→ 传感器检测到目标" << endl;
        } else if (value == 1) {
            cout << " 未触发（高电平）→ 传感器未检测到目标" << endl;
        }
    } else {
        // IO2：下拉输入（未触发=低电平0，触发=高电平1）
        if (value == 1) {
            cout << " 触发（高电平）→ 传感器检测到目标" << endl;
        } else if (value == 0) {
            cout << " 未触发（低电平）→ 传感器未检测到目标" << endl;
        }
    }

    // 打印原始电平值（方便调试）
    cout << "[" << gpioName << "原始值] " << value << "（0=低电平，1=高电平）" << endl;
    return true;
}


// -------------------------- 传感器轮询检测（支持按q退出，参考contant_1.cpp交互逻辑） --------------------------
void PollGpioSensors() {
    cout << "\n=================================================" << endl;
    cout << "            GPIO1/IO2传感器检测（V1.0）" << endl;
    cout << "=================================================" << endl;
    cout << "检测规则：" << endl;
    cout << "  - IO1（上拉）：低电平=触发，高电平=未触发" << endl;
    cout << "  - IO2（下拉）：高电平=触发，低电平=未触发" << endl;
    cout << "操作提示：按'q'并回车退出检测" << endl;
    cout << "=================================================" << endl;

    bool exitFlag = false;
    while (!exitFlag) {
        // 1. 检测是否有键盘输入（按q退出）
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 100000};  // 100ms检测一次键盘

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c;
            cin >> c;
            if (c == 'q' || c == 'Q') {
                exitFlag = true;
                cout << "\n检测已退出，释放资源..." << endl;
                continue;
            } else {
                cout << "无效输入！仅支持按'q'退出" << endl;
            }
        }

        // 2. 读取IO1和IO2状态（0.5秒轮询一次，避免占用过多资源）
        if (!exitFlag) {
            ReadGpioStatus(IO1_SYS_GPIO, "IO1（传感器1）", true);  // IO1=上拉
            ReadGpioStatus(IO2_SYS_GPIO, "IO2（传感器2）", false); // IO2=下拉
            cout << "----------------------------------------" << endl;
            usleep(500000);  // 500ms=0.5秒
        }
    }
}


// -------------------------- 主函数（流程：初始化→检测→退出） --------------------------
int main() {
    // 1. 初始化编码（解决中文乱码）
    InitEncoding();

    // 2. 自动导出并配置IO1和IO2（严格匹配主板特性）
    bool io1Init = ExportAndConfigGPIO(IO1_SYS_GPIO, "IO1（硬件编码GPIO1_A1_u）", "默认上拉");
    bool io2Init = ExportAndConfigGPIO(IO2_SYS_GPIO, "IO2（硬件编码GPIO3_A5_d）", "默认下拉");
    if (!io1Init || !io2Init) {
        cout << "\nGPIO初始化失败，程序无法继续运行" << endl;
        return -1;
    }

    // 3. 启动传感器轮询检测
    PollGpioSensors();

    // 4. 程序退出（无需手动释放GPIO，系统会自动管理）
    cout << "程序已正常退出" << endl;
    return 0;
}