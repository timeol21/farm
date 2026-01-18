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
#include <sys/stat.h>
#include <dirent.h>

using namespace std;

// 全局变量：串口文件描述符（供电磁阀函数使用）
int SerialPortStutas = -1;
// 传感器GPIO路径（可根据实际硬件修改）
const char* gpio_value_path = "/sys/class/gpio/gpio33/value";
// GPIO导出相关路径
const char* gpio_export_path = "/sys/class/gpio/export";
const char* gpio_dir_path = "/sys/class/gpio/gpio33";

// -------------------------- 编码初始化（解决中文乱码） --------------------------
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

// -------------------------- GPIO导出与验证函数 --------------------------
bool ExportGPIOX() {
    cout << "正在自动导出GPIO33..." << endl;

    DIR* dir = opendir(gpio_dir_path);
    if (dir != NULL) {
        closedir(dir);
        cout << "GPIO33已导出，无需重复操作" << endl;
        return true;
    }

    ofstream export_file(gpio_export_path);
    if (!export_file.is_open()) {
        cerr << "GPIO导出文件打开失败！请确保用sudo运行程序" << endl;
        return false;
    }

    export_file << "33";
    export_file.close();

    usleep(100000);

    dir = opendir(gpio_dir_path);
    if (dir == NULL) {
        cerr << "GPIO33导出失败！请检查GPIO编号是否正确" << endl;
        return false;
    }

    closedir(dir);
    cout << "GPIO33导出成功！" << endl;
    return true;
}

// -------------------------- 串口配置相关函数 --------------------------
bool ConfigureSerial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("串口参数设置失败（tcsetattr）");
        return false;
    }
    return true;
}

bool InitSerial(const char *portName = "/dev/ttyS4") {
    if (SerialPortStutas >= 0) {
        cout << "串口已处于初始化状态" << endl;
        return true;
    }

    SerialPortStutas = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (SerialPortStutas < 0) {
        perror("串口打开失败");
        return false;
    }

    if (!ConfigureSerial(SerialPortStutas)) {
        close(SerialPortStutas);
        SerialPortStutas = -1;
        return false;
    }

    cout << "串口初始化成功（设备：" << portName << "）" << endl;
    return true;
}

void CloseSerial() {
    if (SerialPortStutas >= 0) {
        close(SerialPortStutas);
        SerialPortStutas = -1;
        cout << "串口已关闭" << endl;
    }
}

// -------------------------- 电磁阀1（Y0）控制函数 --------------------------
bool OpenSolenoidValve() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀1开启指令发送失败");
        return false;
    }

    cout << "电磁阀1开启指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀1状态：已开启" << endl;
    return true;
}

bool CloseSolenoidValve() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀1关闭指令发送失败");
        return false;
    }

    cout << "电磁阀1关闭指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀1状态：已关闭" << endl;
    return true;
}

// 电磁阀1状态查询（完全仿照开关函数风格）
bool QuerySolenoidValve1Status() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀1查询指令（功能码0x01，查询Y0线圈状态，地址0x0500）
    unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x00, 0x00, 0x01, 0xFD, 0x06};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀1状态查询指令发送失败");
        return false;
    }

    cout << "电磁阀1状态查询指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        // 解析响应：第3字节为数据长度，第4字节bit0表示状态（1=开启，0=关闭）
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                cout << "电磁阀1当前状态：开启" << endl;
            } else {
                cout << "电磁阀1当前状态：关闭" << endl;
            }
        } else {
            cout << "电磁阀1状态查询响应无效" << endl;
        }
    } else {
        cout << "未接收到电磁阀1状态响应" << endl;
    }

    return true;
}

// -------------------------- 电磁阀2（Y1）控制函数 --------------------------
// 完全仿照电磁阀1的代码风格，仅修改地址和校验码
bool OpenSolenoidValve2() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀2开启指令（Y1地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀2开启指令发送失败");
        return false;
    }

    cout << "电磁阀2开启指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀2状态：已开启" << endl;
    return true;
}

bool CloseSolenoidValve2() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀2关闭指令（Y1地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀2关闭指令发送失败");
        return false;
    }

    cout << "电磁阀2关闭指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀2状态：已关闭" << endl;
    return true;
}

// 电磁阀2状态查询（完全仿照电磁阀1查询函数风格）
bool QuerySolenoidValve2Status() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀2查询指令（功能码0x01，查询Y1线圈状态，地址0x0501）
    unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀2状态查询指令发送失败");
        return false;
    }

    cout << "电磁阀2状态查询指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        // 解析响应：第3字节为数据长度，第4字节bit0表示状态（1=开启，0=关闭）
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                cout << "电磁阀2当前状态：开启" << endl;
            } else {
                cout << "电磁阀2当前状态：关闭" << endl;
            }
        } else {
            cout << "电磁阀2状态查询响应无效" << endl;
        }
    } else {
        cout << "未接收到电磁阀2状态响应" << endl;
    }

    return true;
}

// -------------------------- 传感器检测函数 --------------------------
void DetectSensorStatus() {
    cout << "传感器检测已启动（每0.5秒读取一次），按'q'并回车退出检测" << endl;
    cout << "------------------------------------------------" << endl;

    bool exitFlag = false;
    while (!exitFlag) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 100000};

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c;
            cin >> c;
            if (c == 'q' || c == 'Q') {
                exitFlag = true;
                cout << "------------------------------------------------" << endl;
                cout << "传感器检测已退出" << endl;
                continue;
            } else {
                cout << "无效输入，按'q'并回车退出检测" << endl;
            }
        }

        if (!exitFlag) {
            std::ifstream gpio_file(gpio_value_path);
            if (!gpio_file.is_open()) {
                cerr << "传感器GPIO文件打开失败（检查路径或权限）" << endl;
                usleep(500000);
                continue;
            }

            int value;
            gpio_file >> value;

            if (value == 0) {
                cout << "传感器状态：ON（低电平）" << endl;
            } else if (value == 1) {
                cout << "传感器状态：OFF（高电平）" << endl;
            } else {
                cout << "传感器状态：未知（数值：" << value << "）" << endl;
            }

            gpio_file.close();
            usleep(500000);
        }
    }
}

// -------------------------- 主菜单打印函数 --------------------------
void PrintMainMenu() {
    cout << "\n=================================================" << endl;
    cout << "            设备控制中心（V2.0）" << endl;
    cout << "=================================================" << endl;
    cout << "功能选择：" << endl;
    cout << "  1 - 电磁阀1（Y0）控制（开启/关闭/查询）" << endl;
    cout << "  2 - 电磁阀2（Y1）控制（开启/关闭/查询）" << endl;
    cout << "  3 - 传感器状态检测" << endl;
    cout << "  4 - 查询所有电磁阀状态" << endl;
    cout << "  0 - 退出系统" << endl;
    cout << "=================================================" << endl;
}

// -------------------------- 主菜单交互逻辑 --------------------------
int main() {
    InitEncoding();

    if (!ExportGPIOX()) {
        cout << "GPIO初始化失败，程序无法正常运行传感器检测功能" << endl;
        return -1;
    }

    int mainChoice;
    while (true) {
        PrintMainMenu();

        cout << "请输入功能编号（0-4）：";
        cin >> mainChoice;

        switch (mainChoice) {
            // 电磁阀1（Y0）控制（完全保留原逻辑）
            case 1: {
                cout << "\n---------------- 电磁阀1（Y0）控制 ----------------" << endl;
                cout << "  1 - 开启电磁阀" << endl;
                cout << "  2 - 关闭电磁阀" << endl;
                cout << "  3 - 查询当前状态" << endl;
                cout << "  0 - 返回主菜单" << endl;
                cout << "--------------------------------------------" << endl;

                int valveChoice;
                while (true) {
                    cout << "请输入控制指令（0-3）：";
                    cin >> valveChoice;

                    if (valveChoice == 0) {
                        cout << "返回主菜单..." << endl;
                        break;
                    }

                    if (SerialPortStutas < 0) {
                        cout << "正在初始化串口..." << endl;
                        if (!InitSerial()) {
                            cout << "串口初始化失败，无法进行电磁阀控制" << endl;
                            break;
                        }
                    }

                    switch (valveChoice) {
                        case 1: OpenSolenoidValve(); break;
                        case 2: CloseSolenoidValve(); break;
                        case 3: QuerySolenoidValve1Status(); break;
                        default: cout << "无效指令，请输入0-3之间的数字" << endl; break;
                    }
                }
                break;
            }

            // 电磁阀2（Y1）控制（完全仿照电磁阀1逻辑）
            case 2: {
                cout << "\n---------------- 电磁阀2（Y1）控制 ----------------" << endl;
                cout << "  1 - 开启电磁阀" << endl;
                cout << "  2 - 关闭电磁阀" << endl;
                cout << "  3 - 查询当前状态" << endl;
                cout << "  0 - 返回主菜单" << endl;
                cout << "--------------------------------------------" << endl;

                int valveChoice;
                while (true) {
                    cout << "请输入控制指令（0-3）：";
                    cin >> valveChoice;

                    if (valveChoice == 0) {
                        cout << "返回主菜单..." << endl;
                        break;
                    }

                    if (SerialPortStutas < 0) {
                        cout << "正在初始化串口..." << endl;
                        if (!InitSerial()) {
                            cout << "串口初始化失败，无法进行电磁阀控制" << endl;
                            break;
                        }
                    }

                    switch (valveChoice) {
                        case 1: OpenSolenoidValve2(); break;
                        case 2: CloseSolenoidValve2(); break;
                        case 3: QuerySolenoidValve2Status(); break;
                        default: cout << "无效指令，请输入0-3之间的数字" << endl; break;
                    }
                }
                break;
            }

            case 3:
                DetectSensorStatus();
                break;

            // 新增：查询所有电磁阀状态（方便批量查看）
            case 4: {
                cout << "\n---------------- 查询所有电磁阀状态 ----------------" << endl;
                if (SerialPortStutas < 0) {
                    cout << "正在初始化串口..." << endl;
                    if (!InitSerial()) {
                        cout << "串口初始化失败，无法查询状态" << endl;
                        break;
                    }
                }
                QuerySolenoidValve1Status();
                cout << "------------------------------------------------" << endl;
                QuerySolenoidValve2Status();
                break;
            }

            case 0:
                cout << "\n正在退出系统，释放资源..." << endl;
                CloseSerial();
                cout << "系统已退出，感谢使用！" << endl;
                return 0;

            default:
                cout << "无效功能编号，请输入0-4之间的数字" << endl;
                break;
        }
    }
}