#include <iostream>
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

// 串口配置函数（与电磁阀控制程序完全一致）
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

// 初始化串口（与电磁阀控制程序一致，默认使用 /dev/ttyS4）
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

// 读取 X0 状态（核心函数，使用你提供的正确报文）
bool readX0Status() {
    if (serialFd < 0) {
        cerr << "Error: Serial port not initialized" << endl;
        return false;
    }

    // 你之前成功的报文：功能码0x01，读取X0（地址0x0400）状态
    unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("write (read X0)");
        return false;
    }

    // 打印发送的报文
    cout << "Read X0 command sent: ";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收PLC响应
    unsigned char recvBuf[256];
    memset(recvBuf, 0, sizeof(recvBuf)); // 清空接收缓冲区
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    if (len < 0) {
        perror("read (X0 response)");
        return false;
    } else if (len == 0) {
        cout << "No response received (timeout)" << endl;
        return true; // 超时不算失败，继续轮询
    } else {
        // 打印接收的响应
        cout << "X0 response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;

        // 解析X0状态（响应第4字节为状态：0=低电平，1=高电平）
        if (len >= 4 && recvBuf[1] == 0x01) { // 确保是功能码0x01的有效响应
            uint8_t x0State = recvBuf[3];
            cout << "X0 Status: ";
            if (x0State == 0) {
                cout << "Low Level (Sensor not triggered)" << endl;
            } else if (x0State == 1) {
                cout << "High Level (Sensor triggered)" << endl;
            } else {
                cout << "Unknown (Value: " << (int)x0State << ")" << endl;
            }
        } else {
            cout << "Invalid response format" << endl;
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

// 主函数：循环轮询X0状态
int main() {
    // 初始化串口
    InitEncoding();
    if (!initSerial()) {
        return 1;
    }

    cout << "=== X0 Water Level Sensor Monitoring Started ===" << endl;
    cout << "Polling interval: 1 second | Press Ctrl+C to exit" << endl;
    cout << "===============================================" << endl << endl;

    // 循环轮询（持续读取X0状态）
    while (true) {
        readX0Status();
        this_thread::sleep_for(chrono::seconds(1)); // 1秒轮询一次
        cout << "---------------------------------------------" << endl;
    }

    // 关闭串口（实际通过Ctrl+C退出，此处为规范写法）
    closeSerial();
    return 0;
}

/*
用-lmodbus库的话，就是这个编译命令：
ztl@RK356X:~/program/boxfiles/class/testclass$ g++ -o read_water_level read_water_level.cpp -lmodbus
ztl@RK356X:~/program/boxfiles/class/testclass$ sudo ./read_water_level
*/