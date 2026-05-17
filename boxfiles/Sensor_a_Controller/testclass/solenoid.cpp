#include <iostream>                      //可用，通过plc控制电磁阀
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>

using namespace std;

// 全局变量：串口文件描述符（供电磁阀函数使用）
int SerialPortStutas = -1;

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

// -------------------------- 串口配置相关函数 --------------------------
bool ConfigureSerial(int fd) {
    // 当串口成功打开的时候，这个fd的值应该为3。
    /*
    fd = 3 → 代表这个串口的编号是 3
    Linux 不叫 COM1、COM2，它用数字代表每个打开的设备：
    fd = 0 → 键盘
    fd = 1 → 屏幕输出
    fd = 2 → 错误输出
    fd = 3 → 你打开的 /dev/ttyS4 串口
    */
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    cfsetospeed(&tty, B19200);
    cfsetispeed(&tty, B19200);

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

bool InitSerial(const char *portName = "/dev/ttyUSB1") {
    if (SerialPortStutas >= 0) {
        cout << "串口已处于初始化状态" << endl;
        return true;
    }

    SerialPortStutas = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    // 只要打开成功，SerialPortStutas 就等于 3
    /*
    因为 Linux 系统有固定规则：程序启动时，系统自动占用 3 个文件描述符：
    0 = 标准输入（键盘）
    1 = 标准输出（屏幕）
    2 = 标准错误（报错信息）
    这三个是系统天生就占用的，你没打开任何文件，它们也存在。
    所以：你自己打开的第一个文件 / 串口 → 系统必须从 3 开始分配
    */
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

// -------------------------- 电磁阀（Y1）控制函数 --------------------------
bool OpenSolenoidValve() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀（Y1）开启指令（地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀开启指令发送失败");
        return false;
    }

    cout << "电磁阀开启指令已发送：";
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

    cout << "电磁阀状态：已开启" << endl;
    return true;
}

bool CloseSolenoidValve() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀（Y1）关闭指令（地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀关闭指令发送失败");
        return false;
    }

    cout << "电磁阀关闭指令已发送：";
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

    cout << "电磁阀状态：已关闭" << endl;
    return true;
}

// 电磁阀（Y1）状态查询
bool QuerySolenoidValveStatus() {
    if (SerialPortStutas < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    // 电磁阀（Y1）查询指令（功能码0x01，查询Y1线圈状态，地址0x0501）
    unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀状态查询指令发送失败");
        return false;
    }

    cout << "电磁阀状态查询指令已发送：";
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
                cout << "电磁阀当前状态：开启" << endl;
            } else {
                cout << "电磁阀当前状态：关闭" << endl;
            }
        } else {
            cout << "电磁阀状态查询响应无效" << endl;
        }
    } else {
        cout << "未接收到电磁阀状态响应" << endl;
    }

    return true;
}

// -------------------------- 主菜单打印函数 --------------------------
void PrintMainMenu() {
    cout << "\n=================================================" << endl;
    cout << "            485-ttys4 串口通信控制（V1.0）" << endl;
    cout << "=================================================" << endl;
    cout << "功能选择：" << endl;
    cout << "  1 - 电磁阀（Y1）控制（开启/关闭/查询）" << endl;
    cout << "  0 - 退出系统" << endl;
    cout << "=================================================" << endl;
}

// -------------------------- 主函数 --------------------------
int main() {
    InitEncoding();

    int mainChoice;
    while (true) {
        PrintMainMenu();

        cout << "请输入功能编号（0-1）：";
        cin >> mainChoice;

        switch (mainChoice) {
            // 电磁阀（Y1）控制
            case 1: {
                cout << "\n---------------- 电磁阀（Y1）控制 ----------------" << endl;
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
                        case 3: QuerySolenoidValveStatus(); break;
                        default: cout << "无效指令，请输入0-3之间的数字" << endl; break;
                    }
                }
                break;
            }

            case 0:
                cout << "\n正在退出系统，释放资源..." << endl;
                CloseSerial();
                cout << "系统已退出，感谢使用！" << endl;
                return 0;

            default:
                cout << "无效功能编号，请输入0-1之间的数字" << endl;
                break;
        }
    }
}



// #include <iostream>
// #include <fcntl.h>
// #include <unistd.h>
// #include <termios.h>
// #include <cstring>
// #include <chrono>
// #include <thread>
// #include <sys/select.h>
// #include <stdio.h>
// #include <locale.h>

// using namespace std;

// // Global variable: serial port file descriptor
// int SerialPortStutas = -1;

// // -------------------------- Encoding Initialization --------------------------
// void InitEncoding() {
//     const char* locales[] = {"en_US.UTF-8", "C.UTF-8", "POSIX"};
//     bool localeSet = false;

//     for (const char* loc : locales) {
//         if (setlocale(LC_ALL, loc) != NULL) {
//             cout << "Encoding initialized successfully: using " << loc << endl;
//             localeSet = true;
//             break;
//         }
//         cerr << "Failed to set " << loc << ", trying next..." << endl;
//     }

//     if (!localeSet) {
//         perror("All encoding settings failed, print may be garbled");
//     }
// }

// // -------------------------- Serial Port Configuration --------------------------
// bool ConfigureSerial(int fd) {
//     struct termios tty;
//     if (tcgetattr(fd, &tty) != 0) {
//         perror("Failed to get serial attributes (tcgetattr)");
//         return false;
//     }

//     cfsetospeed(&tty, B9600);
//     cfsetispeed(&tty, B9600);

//     tty.c_cflag &= ~PARENB;
//     tty.c_cflag &= ~CSTOPB;
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;
//     tty.c_cflag &= ~CRTSCTS;
//     tty.c_cflag |= CREAD | CLOCAL;

//     tty.c_iflag &= ~(IXON | IXOFF | IXANY);
//     tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
//     tty.c_oflag = 0;
//     tty.c_lflag = 0;

//     tty.c_cc[VMIN] = 0;
//     tty.c_cc[VTIME] = 10;

//     if (tcsetattr(fd, TCSANOW, &tty) != 0) {
//         perror("Failed to set serial attributes (tcsetattr)");
//         return false;
//     }
//     return true;
// }

// bool InitSerial(const char *portName = "/dev/ttyS4") {
//     if (SerialPortStutas >= 0) {
//         cout << "Serial port is already initialized" << endl;
//         return true;
//     }

//     SerialPortStutas = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
//     if (SerialPortStutas < 0) {
//         perror("Failed to open serial port");
//         return false;
//     }

//     if (!ConfigureSerial(SerialPortStutas)) {
//         close(SerialPortStutas);
//         SerialPortStutas = -1;
//         return false;
//     }

//     cout << "Serial port initialized successfully (Device: " << portName << ")" << endl;
//     return true;
// }

// void CloseSerial() {
//     if (SerialPortStutas >= 0) {
//         close(SerialPortStutas);
//         SerialPortStutas = -1;
//         cout << "Serial port closed" << endl;
//     }
// }

// // -------------------------- Solenoid Valve (Y1) Control --------------------------
// bool OpenSolenoidValve() {
//     if (SerialPortStutas < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }

//     unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
//     ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

//     if (sent != (ssize_t)sizeof(sendBuf)) {
//         perror("Failed to send open command");
//         return false;
//     }

//     cout << "Open command sent: ";
//     for (size_t i = 0; i < sizeof(sendBuf); ++i)
//         printf("%02X ", sendBuf[i]);
//     cout << endl;

//     unsigned char recvBuf[256];
//     ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i)
//             printf("%02X ", recvBuf[i]);
//         cout << endl;
//     }

//     cout << "Solenoid valve: Opened" << endl;
//     return true;
// }

// bool CloseSolenoidValve() {
//     if (SerialPortStutas < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }

//     unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
//     ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

//     if (sent != (ssize_t)sizeof(sendBuf)) {
//         perror("Failed to send close command");
//         return false;
//     }

//     cout << "Close command sent: ";
//     for (size_t i = 0; i < sizeof(sendBuf); ++i)
//         printf("%02X ", sendBuf[i]);
//     cout << endl;

//     unsigned char recvBuf[256];
//     ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i)
//             printf("%02X ", recvBuf[i]);
//         cout << endl;
//     }

//     cout << "Solenoid valve: Closed" << endl;
//     return true;
// }

// bool QuerySolenoidValveStatus() {
//     if (SerialPortStutas < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }

//     unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
//     ssize_t sent = write(SerialPortStutas, sendBuf, sizeof(sendBuf));

//     if (sent != (ssize_t)sizeof(sendBuf)) {
//         perror("Failed to send status query command");
//         return false;
//     }

//     cout << "Status query command sent: ";
//     for (size_t i = 0; i < sizeof(sendBuf); ++i)
//         printf("%02X ", sendBuf[i]);
//     cout << endl;

//     unsigned char recvBuf[256];
//     ssize_t len = read(SerialPortStutas, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i)
//             printf("%02X ", recvBuf[i]);
//         cout << endl;

//         if (len >= 4) {
//             if (recvBuf[3] & 0x01) {
//                 cout << "Current status: Opened" << endl;
//             } else {
//                 cout << "Current status: Closed" << endl;
//             }
//         } else {
//             cout << "Invalid status response" << endl;
//         }
//     } else {
//         cout << "No status response received" << endl;
//     }

//     return true;
// }

// // -------------------------- Main Menu --------------------------
// void PrintMainMenu() {
//     cout << "\n=================================================" << endl;
//     cout << "            RS485-ttys4 Serial Control (V1.0)" << endl;
//     cout << "=================================================" << endl;
//     cout << "Menu:" << endl;
//     cout << "  1 - Solenoid Valve (Y1) Control" << endl;
//     cout << "  0 - Exit" << endl;
//     cout << "=================================================" << endl;
// }

// // -------------------------- Main Function --------------------------
// int main() {
//     InitEncoding();

//     int mainChoice;
//     while (true) {
//         PrintMainMenu();

//         cout << "Enter choice (0-1): ";
//         cin >> mainChoice;

//         switch (mainChoice) {
//             case 1: {
//                 cout << "\n---------------- Solenoid Valve (Y1) ----------------" << endl;
//                 cout << "  1 - Open Valve" << endl;
//                 cout << "  2 - Close Valve" << endl;
//                 cout << "  3 - Query Status" << endl;
//                 cout << "  0 - Back to Main Menu" << endl;
//                 cout << "--------------------------------------------------------" << endl;

//                 int valveChoice;
//                 while (true) {
//                     cout << "Enter command (0-3): ";
//                     cin >> valveChoice;

//                     if (valveChoice == 0) {
//                         cout << "Returning to main menu..." << endl;
//                         break;
//                     }

//                     if (SerialPortStutas < 0) {
//                         cout << "Initializing serial port..." << endl;
//                         if (!InitSerial()) {
//                             cout << "Serial init failed, cannot control valve" << endl;
//                             break;
//                         }
//                     }

//                     switch (valveChoice) {
//                         case 1: OpenSolenoidValve(); break;
//                         case 2: CloseSolenoidValve(); break;
//                         case 3: QuerySolenoidValveStatus(); break;
//                         default: cout << "Invalid command, enter 0-3" << endl; break;
//                     }
//                 }
//                 break;
//             }

//             case 0:
//                 cout << "\nExiting system, releasing resources..." << endl;
//                 CloseSerial();
//                 cout << "System exited" << endl;
//                 return 0;

//             default:
//                 cout << "Invalid choice, enter 0-1" << endl;
//                 break;
//         }
//     }
// }