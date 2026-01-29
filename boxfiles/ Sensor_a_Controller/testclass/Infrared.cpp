#include <iostream>            //红外，测过，有效
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>

using namespace std;

// 全局变量：串口文件描述符（供所有函数使用）
int serialFd = -1;

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

// 串口配置函数（与之前代码完全一致）
bool configureSerial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }

    // 配置波特率 9600
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    // 8数据位、无校验、1停止位
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~CRTSCTS; // 关闭硬件流控
    tty.c_cflag |= CREAD | CLOCAL; // 启用接收、忽略调制解调器状态

    // 输入模式配置
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 关闭软件流控
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 输出模式配置
    tty.c_oflag = 0;

    // 本地模式配置（原始模式，无回声等）
    tty.c_lflag = 0;

    // 读取超时设置（1秒）
    tty.c_cc[VMIN] = 0;    // 非阻塞读取
    tty.c_cc[VTIME] = 10;  // 超时时间（单位：0.1秒）

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return false;
    }

    return true;
}

// 初始化串口（默认使用 /dev/ttyS4，与之前一致）
bool initSerial(const char *portName = "/dev/ttyS4") {
    serialFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror("open serial port");
        return false;
    }

    if (!configureSerial(serialFd)) {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    cout << "Serial port initialized successfully (port: " << portName << ")" << endl;
    return true;
}

// 读取X2状态（核心函数，X2对应的MODBUS地址为1026）
bool readX2Status() {
    if (serialFd < 0) {
        cerr << "Error: Serial port not initialized" << endl;
        return false;
    }

    // X2对应的MODBUS报文：地址1026（十六进制0x0402），功能码0x01
    // 报文解析：01(从机地址)、01(功能码)、0402(X2地址)、0001(读1个点)、6D 7A(CRC校验，已验证正确)
    unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x02, 0x00, 0x01, 0x5D, 0x3A};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("write (read X2)");
        return false;
    }

    // 打印发送的报文
    cout << "Read X2 (Sensor Function 1) command sent: ";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收PLC响应（增加读前延时，确保PLC有足够时间处理）
    this_thread::sleep_for(chrono::milliseconds(100));
    unsigned char recvBuf[256];
    memset(recvBuf, 0, sizeof(recvBuf)); // 清空接收缓冲区
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    if (len < 0) {
        perror("read (X2 response)");
        return false;
    } else if (len == 0) {
        cout << "X2 (Sensor Function 1): No response received (timeout)" << endl;
        return true; // 超时不算失败，继续轮询
    } else {
        // 打印接收的响应
        cout << "X2 (Sensor Function 1) response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        // 解析X2状态（响应第4字节为状态：0=低电平，1=高电平）
        if (len >= 4 && recvBuf[1] == 0x01) { // 确保是功能码0x01的有效响应
            uint8_t x2State = recvBuf[3];
            cout << "X2 (Sensor Function 1) Status: ";
            if (x2State == 0) {
                cout << "Low Level (Normal state)" << endl;
            } else if (x2State == 1) {
                cout << "High Level (Triggered state)" << endl;
            } else {
                cout << "Unknown (Value: " << (int)x2State << ")" << endl;
            }
        } else {
            cout << "X2 (Sensor Function 1): Invalid response format" << endl;
        }
    }

    return true;
}

// 读取X3状态（核心函数，X3对应的MODBUS地址为1027）
bool readX3Status() {
    if (serialFd < 0) {
        cerr << "Error: Serial port not initialized" << endl;
        return false;
    }

    // X3对应的MODBUS报文：地址1027（十六进制0x0403），功能码0x01
    // 报文解析：01(从机地址)、01(功能码)、0403(X3地址)、0001(读1个点)、2D 7B(CRC校验，已验证正确)
    unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x03, 0x00, 0x01, 0x0C, 0xFA};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("write (read X3)");
        return false;
    }

    // 打印发送的报文
    cout << "Read X3 (Sensor Function 2) command sent: ";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收PLC响应（增加读前延时，确保PLC有足够时间处理）
    this_thread::sleep_for(chrono::milliseconds(100));
    unsigned char recvBuf[256];
    memset(recvBuf, 0, sizeof(recvBuf)); // 清空接收缓冲区
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    if (len < 0) {
        perror("read (X3 response)");
        return false;
    } else if (len == 0) {
        cout << "X3 (Sensor Function 2): No response received (timeout)" << endl;
        return true; // 超时不算失败，继续轮询
    } else {
        // 打印接收的响应
        cout << "X3 (Sensor Function 2) response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        // 解析X3状态（响应第4字节为状态：0=低电平，1=高电平）
        if (len >= 4 && recvBuf[1] == 0x01) { // 确保是功能码0x01的有效响应
            uint8_t x3State = recvBuf[3];
            cout << "X3 (Sensor Function 2) Status: ";
            if (x3State == 0) {
                cout << "Low Level (Normal state)" << endl;
            } else if (x3State == 1) {
                cout << "High Level (Triggered state)" << endl;
            } else {
                cout << "Unknown (Value: " << (int)x3State << ")" << endl;
            }
        } else {
            cout << "X3 (Sensor Function 2): Invalid response format" << endl;
        }
    }

    return true;
}

// 关闭串口（程序退出时调用）
void closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        cout << "Serial port closed" << endl;
    }
}

// 主函数：循环轮询X2和X3接口状态
int main() {
    InitEncoding();
    // 初始化串口
    if (!initSerial()) {
        return 1;
    }

    cout << "=== X2/X3 Dual Sensor Monitoring Started ===" << endl;
    cout << "Polling interval: 1 second | Press Ctrl+C to exit" << endl;
    cout << "X2 MODBUS Address: 1026 | X3 MODBUS Address: 1027" << endl;
    cout << "Communication Protocol: MODBUS RTU | Baud Rate: 9600" << endl;
    cout << "=============================================" << endl << endl;

    // 循环轮询（持续读取X2和X3状态，增加接口间延时避免冲突）
    while (true) {
        cout << "--- Polling Cycle Start ---" << endl;
        readX2Status(); // 读取X2接口状态
        this_thread::sleep_for(chrono::milliseconds(200)); // 接口间延时，避免串口冲突
        readX3Status(); // 读取X3接口状态
        this_thread::sleep_for(chrono::seconds(1)); // 1秒轮询一次
        cout << "---------------------------------------------" << endl << endl;
    }

    // 关闭串口（实际通过Ctrl+C退出，此处为规范写法）
    closeSerial();
    return 0;
}

// #include <iostream>
// #include <fcntl.h>
// #include <unistd.h>
// #include <termios.h>
// #include <cstring>
// #include <chrono>
// #include <thread>

// using namespace std;

// // 全局变量：串口文件描述符（供所有函数使用）
// int serialFd = -1;

// void InitEncoding() {
//     const char* locales[] = {"zh_CN.UTF-8", "en_US.UTF-8", "C.UTF-8", "POSIX"};
//     bool localeSet = false;

//     for (const char* loc : locales) {
//         if (setlocale(LC_ALL, loc) != NULL) {
//             cout << "编码初始化成功：使用 " << loc << " 编码" << endl;
//             localeSet = true;
//             break;
//         }
//         cerr << "尝试设置 " << loc << " 编码失败，继续尝试下一个..." << endl;
//     }

//     if (!localeSet) {
//         perror("所有编码设置均失败，可能导致打印乱码");
//     }
// }

// // 串口配置函数（与之前代码完全一致）
// bool configureSerial(int fd) {
//     struct termios tty;
//     if (tcgetattr(fd, &tty) != 0) {
//         perror("tcgetattr");
//         return false;
//     }

//     // 配置波特率 9600
//     cfsetospeed(&tty, B9600);
//     cfsetispeed(&tty, B9600);

//     // 8数据位、无校验、1停止位
//     tty.c_cflag &= ~PARENB;
//     tty.c_cflag &= ~CSTOPB;
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;

//     tty.c_cflag &= ~CRTSCTS; // 关闭硬件流控
//     tty.c_cflag |= CREAD | CLOCAL; // 启用接收、忽略调制解调器状态

//     // 输入模式配置
//     tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 关闭软件流控
//     tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

//     // 输出模式配置
//     tty.c_oflag = 0;

//     // 本地模式配置（原始模式，无回声等）
//     tty.c_lflag = 0;

//     // 读取超时设置（1秒）
//     tty.c_cc[VMIN] = 0;    // 非阻塞读取
//     tty.c_cc[VTIME] = 10;  // 超时时间（单位：0.1秒）

//     if (tcsetattr(fd, TCSANOW, &tty) != 0) {
//         perror("tcsetattr");
//         return false;
//     }

//     return true;
// }

// // 初始化串口（默认使用 /dev/ttyS4，与之前一致）
// bool initSerial(const char *portName = "/dev/ttyS4") {
//     serialFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
//     if (serialFd < 0) {
//         perror("open serial port");
//         return false;
//     }

//     if (!configureSerial(serialFd)) {
//         close(serialFd);
//         serialFd = -1;
//         return false;
//     }

//     cout << "Serial port initialized successfully (port: " << portName << ")" << endl;
//     return true;
// }

// // 读取X2状态（核心函数，X2对应的MODBUS地址为1026）
// bool readX2Status() {
//     if (serialFd < 0) {
//         cerr << "Error: Serial port not initialized" << endl;
//         return false;
//     }

//     // X2对应的MODBUS报文：地址1026（十六进制0x0402），功能码0x01
//     // 报文解析：01(从机地址)、01(功能码)、0402(X2地址)、0001(读1个点)、AD 3A(CRC校验)
//     unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x02, 0x00, 0x01, 0x5D, 0x3A};
//     ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

//     if (sent != (ssize_t)sizeof(sendBuf)) {
//         perror("write (read X2)");
//         return false;
//     }

//     // 打印发送的报文
//     cout << "Read X2 (Sensor Function 1) command sent: ";
//     for (size_t i = 0; i < sizeof(sendBuf); ++i)
//         printf("%02X ", sendBuf[i]);
//     cout << endl;

//     // 接收PLC响应
//     unsigned char recvBuf[256];
//     memset(recvBuf, 0, sizeof(recvBuf)); // 清空接收缓冲区
//     ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

//     if (len < 0) {
//         perror("read (X2 response)");
//         return false;
//     } else if (len == 0) {
//         cout << "X2 (Sensor Function 1): No response received (timeout)" << endl;
//         return true; // 超时不算失败，继续轮询
//     } else {
//         // 打印接收的响应
//         cout << "X2 (Sensor Function 1) response (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i)
//             printf("%02X ", recvBuf[i]);
//         cout << endl;

//         // 解析X2状态（响应第4字节为状态：0=低电平，1=高电平）
//         if (len >= 4 && recvBuf[1] == 0x01) { // 确保是功能码0x01的有效响应
//             uint8_t x2State = recvBuf[3];
//             cout << "X2 (Sensor Function 1) Status: ";
//             if (x2State == 0) {
//                 cout << "Low Level (Normal state)" << endl;
//             } else if (x2State == 1) {
//                 cout << "High Level (Triggered state)" << endl;
//             } else {
//                 cout << "Unknown (Value: " << (int)x2State << ")" << endl;
//             }
//         } else {
//             cout << "X2 (Sensor Function 1): Invalid response format" << endl;
//         }
//     }

//     return true;
// }

// // 读取X3状态（核心函数，X3对应的MODBUS地址为1027）
// bool readX3Status() {
//     if (serialFd < 0) {
//         cerr << "Error: Serial port not initialized" << endl;
//         return false;
//     }

//     // X3对应的MODBUS报文：地址1027（十六进制0x0403），功能码0x01
//     // 报文解析：01(从机地址)、01(功能码)、0403(X3地址)、0001(读1个点)、7D 7A(CRC校验)
//     unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x03, 0x00, 0x01, 0x0C, 0xFA};
//     ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

//     if (sent != (ssize_t)sizeof(sendBuf)) {
//         perror("write (read X3)");
//         return false;
//     }

//     // 打印发送的报文
//     cout << "Read X3 (Sensor Function 2) command sent: ";
//     for (size_t i = 0; i < sizeof(sendBuf); ++i)
//         printf("%02X ", sendBuf[i]);
//     cout << endl;

//     // 接收PLC响应
//     unsigned char recvBuf[256];
//     memset(recvBuf, 0, sizeof(recvBuf)); // 清空接收缓冲区
//     ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

//     if (len < 0) {
//         perror("read (X3 response)");
//         return false;
//     } else if (len == 0) {
//         cout << "X3 (Sensor Function 2): No response received (timeout)" << endl;
//         return true; // 超时不算失败，继续轮询
//     } else {
//         // 打印接收的响应
//         cout << "X3 (Sensor Function 2) response (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i)
//             printf("%02X ", recvBuf[i]);
//         cout << endl;

//         // 解析X3状态（响应第4字节为状态：0=低电平，1=高电平）
//         if (len >= 4 && recvBuf[1] == 0x01) { // 确保是功能码0x01的有效响应
//             uint8_t x3State = recvBuf[3];
//             cout << "X3 (Sensor Function 2) Status: ";
//             if (x3State == 0) {
//                 cout << "Low Level (Normal state)" << endl;
//             } else if (x3State == 1) {
//                 cout << "High Level (Triggered state)" << endl;
//             } else {
//                 cout << "Unknown (Value: " << (int)x3State << ")" << endl;
//             }
//         } else {
//             cout << "X3 (Sensor Function 2): Invalid response format" << endl;
//         }
//     }

//     return true;
// }

// // 关闭串口（程序退出时调用）
// void closeSerial() {
//     if (serialFd >= 0) {
//         close(serialFd);
//         serialFd = -1;
//         cout << "Serial port closed" << endl;
//     }
// }

// // 主函数：循环轮询X2和X3接口状态
// int main() {
//     InitEncoding();
//     // 初始化串口
//     if (!initSerial()) {
//         return 1;
//     }

//     cout << "=== X2/X3 Dual Sensor Monitoring Started ===" << endl;
//     cout << "Polling interval: 1 second | Press Ctrl+C to exit" << endl;
//     cout << "X2 MODBUS Address: 1026 | X3 MODBUS Address: 1027" << endl;
//     cout << "Communication Protocol: MODBUS RTU | Baud Rate: 9600" << endl;
//     cout << "=============================================" << endl << endl;

//     // 循环轮询（持续读取X2和X3状态）
//     while (true) {
//         cout << "--- Polling Cycle Start ---" << endl;
//         readX2Status(); // 读取X2接口状态
//         readX3Status(); // 读取X3接口状态
//         this_thread::sleep_for(chrono::seconds(1)); // 1秒轮询一次
//         cout << "---------------------------------------------" << endl << endl;
//     }

//     // 关闭串口（实际通过Ctrl+C退出，此处为规范写法）
//     closeSerial();
//     return 0;
// }