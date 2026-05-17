#include <iostream>      // 报文硬编码
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>
#include <stdbool.h>
#include <vector>

using namespace std;
int fd = -1;
int baudRate = 19200;
int dataBits = 8;
int stopBits = 1;
char parity = 'N';


bool ConfigureSerial(int fd, int baudRate, int dataBits, char parity, int stopBits)
{
    struct termios tty;

    // Get current serial port attributes
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("Failed to get serial attributes (tcgetattr)");
        return false;
    }

    //==================== Fixed core settings ====================
    // Disable hardware flow control (RTS/CTS)
    tty.c_cflag &= ~CRTSCTS;
    // Enable receiver & local mode, ignore modem control signals
    tty.c_cflag |= CREAD | CLOCAL;

    // Disable software flow control (XON/XOFF)
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Disable all input character translation and special handling
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output mode, no output processing
    tty.c_oflag = 0;

    // Disable canonical mode, echo, signal interrupt and extended functions
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

    // Read timeout setting
    tty.c_cc[VMIN] = 0;     // Minimum read bytes
    tty.c_cc[VTIME] = 10;   // Read timeout: unit 0.1s, total 1s
    //=============================================================

    // Set baud rate
    speed_t speed;
    switch (baudRate)
    {
        case 4800:    speed = B4800;    break;
        case 9600:    speed = B9600;    break;
        case 19200:   speed = B19200;   break;
        case 38400:   speed = B38400;   break;
        case 115200:  speed = B115200;  break;
        default:
            fprintf(stderr, "Unsupported baud rate: %d\n", baudRate);
            return false;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Set data bits: 5/6/7/8
    tty.c_cflag &= ~CSIZE; // Clear old data bit setting
    switch (dataBits)
    {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            fprintf(stderr, "Unsupported data bits: %d\n", dataBits);
            return false;
    }

    // Set parity check mode
    tty.c_cflag &= ~(PARENB | PARODD); // Clear old parity config
    switch (parity)
    {
        case 'N':
        case 'n':
            // No parity check
            break;
        case 'E':
        case 'e':
            // Even parity
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case 'O':
        case 'o':
            // Odd parity
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
        default:
            fprintf(stderr, "Unsupported parity type: %c\n", parity);
            return false;
    }
    // 当这个parity是string类型时
    //     if (parity == "N" || parity == "n")
    // {
    //     // 无校验
    // }
    // else if (parity == "E" || parity == "e")
    // {
    //     // 偶校验
    //     tty.c_cflag |= PARENB;
    //     tty.c_cflag &= ~PARODD;
    // }
    // else if (parity == "O" || parity == "o")
    // {
    //     // 奇校验
    //     tty.c_cflag |= PARENB;
    //     tty.c_cflag |= PARODD;
    // }
    // else
    // {
    //     fprintf(stderr, "Unsupported parity type: %s\n", parity.c_str());
    //     return false;
    // }

    // Set stop bits: 1 or 2
    if (stopBits == 1)
    {
        tty.c_cflag &= ~CSTOPB; // 1 stop bit
    }
    else if (stopBits == 2)
    {
        tty.c_cflag |= CSTOPB;  // 2 stop bits
    }
    else
    {
        fprintf(stderr, "Unsupported stop bits: %d\n", stopBits);
        return false;
    }

    // Apply new serial port settings immediately
    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("Failed to set serial attributes (tcsetattr)");
        return false;
    }

    return true;
}

bool initSerial(const string &portName) {
    // If already opened
    if (fd >= 0) {
        cout << "Serial port is already initialized" << endl;
        return true;
    }

    // Open serial port: convert string to const char*
    fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Failed to open serial port");
        return false;
    }
        // 只要打开成功，fd_ 就等于 3
    /*
    因为 Linux 系统有固定规则：程序启动时，系统自动占用 3 个文件描述符：
    0 = 标准输入（键盘）
    1 = 标准输出（屏幕）
    2 = 标准错误（报错信息）
    这三个是系统天生就占用的，你没打开任何文件，它们也存在。
    所以：你自己打开的第一个文件 / 串口 → 系统必须从 3 开始分配
    */
    // Configure serial port
    if (!ConfigureSerial(fd, baudRate, dataBits, parity, stopBits)) {
        close(fd);
        fd = -1;
        cout << "Failed to configure serial port" << endl;
        return false;
    }

    cout << "Serial port initialized successfully (Device: " << portName << ")" << endl;
    return true;
}

void closeSerial() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
        cout << "Serial port closed" << endl;
    }
}

// 小驼峰 + 指令硬编码
bool openSolenoidValve() {
    // 硬编码：打开电磁阀指令
    std::vector<uint8_t> sendCmd = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};

    // Check if serial port is ready
    if (fd < 0) {
        cerr << "Error: Serial port not initialized, please initialize first" << endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);

    // Send data
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send command");
        return false;
    }

    // Print sent data
    cout << "Command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    cout << endl;

    this_thread::sleep_for(chrono::milliseconds(100));

    // Read response
    uint8_t recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        cout << endl;
    }else {
        cerr << "Warning: Serial port connected, but no response from solenoid valve!" << endl;
    }
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}

// 小驼峰 + 指令硬编码
bool closeSolenoidValve() {
    // 硬编码：关闭电磁阀指令
    std::vector<uint8_t> sendCmd = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};

    // Check if serial port is initialized
    if (fd < 0) {
        cerr << "Error: Serial port not initialized, please initialize first" << endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);

    // Send command data
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send close command");
        return false;
    }

    // Print sent command
    cout << "Close command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    cout << endl;

    this_thread::sleep_for(chrono::milliseconds(100));

    // Read response from serial port
    uint8_t recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        cout << endl;
    }else {
        cerr << "Warning: Serial port connected, but no response received for close command!" << endl;
    }
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}

// 小驼峰 + 指令硬编码
bool querySolenoidValveStatus() {
    // 硬编码：查询状态指令
    std::vector<uint8_t> sendCmd = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};

    // Check if serial port is initialized
    if (fd < 0) {
        cerr << "Error: Serial port not initialized, please initialize first" << endl;
        return false;
    }

    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);

    // Send command data
    ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
    if (sent != (ssize_t)sendCmd.size()) {
        perror("Failed to send status query command");
        return false;
    }

    // Print sent command
    cout << "Status query command sent: ";
    for (uint8_t byte : sendCmd) {
        printf("%02X ", byte);
    }
    cout << endl;

    this_thread::sleep_for(chrono::milliseconds(100));

    // Read response from serial port
    uint8_t recvBuf[256];
    ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "Response received (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        cout << endl;

        // Parse status from response
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                cout << "Current status: Opened" << endl;
            } else {
                cout << "Current status: Closed" << endl;
            }
        } else {
            cout << "Invalid status response" << endl;
        }
    } else {
        cout << "No status response received" << endl;
    }
    
    // this_thread::sleep_for(chrono::milliseconds(100));
    return true;
}

int main() {
    // 1. 设置串口设备
    string port = "/dev/ttyUSB1";

    // 2. 初始化串口
    if (!initSerial(port)) {
        cerr << "Failed to initialize serial port" << endl;
        return -1;
    }

    // 3. 打开电磁阀（无参数）
    cout << "\n=== Open Solenoid Valve ===" << endl;
    openSolenoidValve();

    // 等待一下
    this_thread::sleep_for(chrono::seconds(2));

    // 4. 查询状态（无参数）
    cout << "\n=== Query Solenoid Valve Status ===" << endl;
    querySolenoidValveStatus();

    // 等待一下
    this_thread::sleep_for(chrono::seconds(2));

    // 5. 关闭电磁阀（无参数）
    cout << "\n=== Close Solenoid Valve ===" << endl;
    closeSolenoidValve();

    // 4. 查询状态（无参数）
    cout << "\n=== Query Solenoid Valve Status ===" << endl;
    querySolenoidValveStatus();

    // 等待一下
    this_thread::sleep_for(chrono::seconds(2));

    // 6. 关闭串口
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
// #include <sys/select.h>
// #include <stdio.h>
// #include <locale.h>
// #include <stdbool.h>
// #include <vector>

// using namespace std;
// int fd = -1;
// int baudRate = 19200;
// int dataBits = 8;
// int stopBits = 1;
// char parity = 'N';

// bool ConfigureSerial(int fd, int baudRate, int dataBits, char parity, int stopBits)
// {
//     struct termios tty;

//     // Get current serial port attributes
//     if (tcgetattr(fd, &tty) != 0)
//     {
//         perror("Failed to get serial attributes (tcgetattr)");
//         return false;
//     }

//     //==================== Fixed core settings ====================
//     // Disable hardware flow control (RTS/CTS)
//     tty.c_cflag &= ~CRTSCTS;
//     // Enable receiver & local mode, ignore modem control signals
//     tty.c_cflag |= CREAD | CLOCAL;

//     // Disable software flow control (XON/XOFF)
//     tty.c_iflag &= ~(IXON | IXOFF | IXANY);

//     // Disable all input character translation and special handling
//     tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

//     // Raw output mode, no output processing
//     tty.c_oflag = 0;

//     // Disable canonical mode, echo, signal interrupt and extended functions
//     tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

//     // Read timeout setting
//     tty.c_cc[VMIN] = 0;     // Minimum read bytes
//     tty.c_cc[VTIME] = 10;   // Read timeout: unit 0.1s, total 1s
//     //=============================================================

//     // Set baud rate
//     speed_t speed;
//     switch (baudRate)
//     {
//         case 4800:    speed = B4800;    break;
//         case 9600:    speed = B9600;    break;
//         case 19200:   speed = B19200;   break;
//         case 38400:   speed = B38400;   break;
//         case 115200:  speed = B115200;  break;
//         default:
//             fprintf(stderr, "Unsupported baud rate: %d\n", baudRate);
//             return false;
//     }
//     cfsetispeed(&tty, speed);
//     cfsetospeed(&tty, speed);

//     // Set data bits: 5/6/7/8
//     tty.c_cflag &= ~CSIZE; // Clear old data bit setting
//     switch (dataBits)
//     {
//         case 5: tty.c_cflag |= CS5; break;
//         case 6: tty.c_cflag |= CS6; break;
//         case 7: tty.c_cflag |= CS7; break;
//         case 8: tty.c_cflag |= CS8; break;
//         default:
//             fprintf(stderr, "Unsupported data bits: %d\n", dataBits);
//             return false;
//     }

//     // Set parity check mode
//     tty.c_cflag &= ~(PARENB | PARODD); // Clear old parity config
//     switch (parity)
//     {
//         case 'N':
//         case 'n':
//             // No parity check
//             break;
//         case 'E':
//         case 'e':
//             // Even parity
//             tty.c_cflag |= PARENB;
//             tty.c_cflag &= ~PARODD;
//             break;
//         case 'O':
//         case 'o':
//             // Odd parity
//             tty.c_cflag |= PARENB;
//             tty.c_cflag |= PARODD;
//             break;
//         default:
//             fprintf(stderr, "Unsupported parity type: %c\n", parity);
//             return false;
//     }

//     // Set stop bits: 1 or 2
//     if (stopBits == 1)
//     {
//         tty.c_cflag &= ~CSTOPB; // 1 stop bit
//     }
//     else if (stopBits == 2)
//     {
//         tty.c_cflag |= CSTOPB;  // 2 stop bits
//     }
//     else
//     {
//         fprintf(stderr, "Unsupported stop bits: %d\n", stopBits);
//         return false;
//     }

//     // Apply new serial port settings immediately
//     if (tcsetattr(fd, TCSANOW, &tty) != 0)
//     {
//         perror("Failed to set serial attributes (tcsetattr)");
//         return false;
//     }

//     return true;
// }

// bool InitSerial(const string &portName) {
//     // If already opened
//     if (fd >= 0) {
//         cout << "Serial port is already initialized" << endl;
//         return true;
//     }

//     // Open serial port: convert string to const char*
//     fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
//     if (fd < 0) {
//         perror("Failed to open serial port");
//         return false;
//     }

//     // Configure serial port
//     if (!ConfigureSerial(fd, baudRate, dataBits, parity, stopBits)) {
//         close(fd);
//         fd = -1;
//         cout << "Failed to configure serial port" << endl;
//         return false;
//     }

//     cout << "Serial port initialized successfully (Device: " << portName << ")" << endl;
//     return true;
// }

// void CloseSerial() {
//     if (fd >= 0) {
//         close(fd);
//         fd = -1;
//         cout << "Serial port closed" << endl;
//     }
// }

// bool OpenSolenoidValve(const std::vector<uint8_t>& sendCmd) {
//     // Check if serial port is ready
//     if (fd < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }
//     tcflush(fd, TCIOFLUSH);

//     // Check if command is empty
//     if (sendCmd.empty()) {
//         cerr << "Error: Send command is empty" << endl;
//         return false;
//     }

//     // Send data
//     ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
//     if (sent != (ssize_t)sendCmd.size()) {
//         perror("Failed to send command");
//         return false;
//     }

//     // Print sent data
//     cout << "Command sent: ";
//     for (uint8_t byte : sendCmd) {
//         printf("%02X ", byte);
//     }
//     cout << endl;

//     // Read response
//     uint8_t recvBuf[256];
//     ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i) {
//             printf("%02X ", recvBuf[i]);
//         }
//         cout << endl;
//     }else {
//         cerr << "Warning: Serial port connected, but no response from solenoid valve!" << endl;
//     }
//     return true;
// }


// bool CloseSolenoidValve(const std::vector<uint8_t>& sendCmd) {
//     // Check if serial port is initialized
//     if (fd < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }
//     tcflush(fd, TCIOFLUSH);
//     // Check if command buffer is empty
//     if (sendCmd.empty()) {
//         cerr << "Error: Send command is empty" << endl;
//         return false;
//     }

//     // Send command data
//     ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
//     if (sent != (ssize_t)sendCmd.size()) {
//         perror("Failed to send close command");
//         return false;
//     }

//     // Print sent command
//     cout << "Close command sent: ";
//     for (uint8_t byte : sendCmd) {
//         printf("%02X ", byte);
//     }
//     cout << endl;

//     // Read response from serial port
//     uint8_t recvBuf[256];
//     ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i) {
//             printf("%02X ", recvBuf[i]);
//         }
//         cout << endl;
//     }else {
//         cerr << "Warning: Serial port connected, but no response received for close command!" << endl;
//     }
//     return true;
// }



// bool QuerySolenoidValveStatus(const std::vector<uint8_t>& sendCmd) {
//     // Check if serial port is initialized
//     if (fd < 0) {
//         cerr << "Error: Serial port not initialized, please initialize first" << endl;
//         return false;
//     }
//     tcflush(fd, TCIOFLUSH);
//     // Check if command buffer is empty
//     if (sendCmd.empty()) {
//         cerr << "Error: Send command is empty" << endl;
//         return false;
//     }

//     // Send command data
//     ssize_t sent = write(fd, sendCmd.data(), sendCmd.size());
//     if (sent != (ssize_t)sendCmd.size()) {
//         perror("Failed to send status query command");
//         return false;
//     }

//     // Print sent command
//     cout << "Status query command sent: ";
//     for (uint8_t byte : sendCmd) {
//         printf("%02X ", byte);
//     }
//     cout << endl;

//     // Read response from serial port
//     uint8_t recvBuf[256];
//     ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
//     if (len > 0) {
//         cout << "Response received (" << len << " bytes): ";
//         for (ssize_t i = 0; i < len; ++i) {
//             printf("%02X ", recvBuf[i]);
//         }
//         cout << endl;

//         // Parse status from response
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

// int main() {
//     // 1. 设置串口设备
//     string port = "/dev/ttyUSB0";  // 你可以改成你的设备

//     // 2. 初始化串口
//     if (!InitSerial(port)) {
//         cerr << "Failed to initialize serial port" << endl;
//         return -1;
//     }

//     // ===================== 定义你的三个报文 =====================
//     vector<uint8_t> openCmd    = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
//     vector<uint8_t> closeCmd   = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
//     vector<uint8_t> statusCmd  = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
//     // ============================================================

//     // 3. 打开电磁阀
//     cout << "\n=== Open Solenoid Valve ===" << endl;
//     OpenSolenoidValve(openCmd);

//     // 等待一下
//     this_thread::sleep_for(chrono::seconds(2));

//     // 4. 查询状态
//     cout << "\n=== Query Solenoid Valve Status ===" << endl;
//     QuerySolenoidValveStatus(statusCmd);

//     // 等待一下
//     this_thread::sleep_for(chrono::seconds(2));

//     // 5. 关闭电磁阀
//     cout << "\n=== Close Solenoid Valve ===" << endl;
//     CloseSolenoidValve(closeCmd);

//     // 6. 关闭串口
//     CloseSerial();

//     return 0;
// }