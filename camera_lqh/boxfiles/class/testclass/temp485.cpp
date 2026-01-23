// #include <iostream>      //正确的得到对应的温湿度
// #include <fcntl.h>
// #include <unistd.h>
// #include <termios.h>
// #include <cstring>
// #include <chrono>
// #include <thread>
// #include <iomanip>

// using namespace std;

// int serialFd = -1;

// // 串口配置：9600波特率、8N1、无流控（适配DC-A568-V06串口4）
// bool configureSerial(int fd) {
//     struct termios tty;
//     if (tcgetattr(fd, &tty) != 0) {
//         perror("串口参数获取失败");
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
//         perror("串口参数配置失败");
//         return false;
//     }
//     return true;
// }

// // 初始化串口（固定为/dev/ttyS4）
// bool initSerial() {
//     const char *port = "/dev/ttyS4";
//     serialFd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
//     if (serialFd < 0) {
//         perror("串口打开失败（必须用sudo运行）");
//         return false;
//     }
//     if (!configureSerial(serialFd)) {
//         close(serialFd);
//         serialFd = -1;
//         return false;
//     }
//     cout << "串口" << port << "初始化成功" << endl;
//     return true;
// }

// // 核心修正：交换温湿度寄存器对应关系，匹配传感器实际数据格式
// void readHumiture() {
//     if (serialFd < 0) return;

//     // 发送报文：从机0x01、寄存器0x0000、读2个寄存器（湿度+温度，顺序修正）
//     unsigned char sendBuf[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
//     write(serialFd, sendBuf, sizeof(sendBuf));
//     cout << "发送读取指令：01 03 00 00 00 02 C4 0B" << endl;

//     // 接收传感器响应
//     unsigned char recvBuf[256] = {0};
//     ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

//     // 响应长度9字节（符合MODBUS RTU协议）
//     if (len == 9 && recvBuf[0] == 0x01 && recvBuf[1] == 0x03) {
//         // 修正：第1个寄存器=湿度，第2个寄存器=温度（大端序，数值/10=实际值）
//         uint16_t humiData = (recvBuf[3] << 8) | recvBuf[4];  // 湿度：0x01A8 → 424 → 42.4%RH
//         uint16_t tempData = (recvBuf[5] << 8) | recvBuf[6];  // 温度：0x00D9 → 217 → 21.7℃

//         // 若实际温度需整数显示，将除法去掉（改为 tempData → 217℃ 显然错误，故保留除法）
//         float temperature = tempData / 10.0;
//         float humidity = humiData / 10.0;

//         cout << "读取成功！" << endl;
//         cout << "温度：" << fixed << setprecision(1) << temperature << "℃ " 
//              << "湿度：" << fixed << setprecision(1) << humidity << "%RH" << endl;
//     } else if (len == 0) {
//         cout << "未收到响应（检查接线、传感器地址）" << endl;
//     } else {
//         cout << "响应异常（长度：" << len << "字节）：";
//         for (ssize_t i = 0; i < len; ++i) printf("%02X ", recvBuf[i]);
//         cout << endl;
//     }
//     cout << "----------------------------------------" << endl;
// }

// // 关闭串口
// void closeSerial() {
//     if (serialFd >= 0) {
//         close(serialFd);
//         cout << "串口已关闭" << endl;
//     }
// }

// int main() {
//     if (!initSerial()) return 1;

//     cout << "开始实时温湿度监控（按Ctrl+C停止）" << endl;
//     cout << "----------------------------------------" << endl;

//     while (true) {
//         readHumiture();
//         this_thread::sleep_for(chrono::seconds(1));  // 1秒读取一次
//     }

//     closeSerial();
//     return 0;
// }


#include <iostream>           //正确的得到对应的温湿度，同时可以正确的得到对应的返回报文
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iomanip>

using namespace std;

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

// 串口配置：9600波特率、8N1、无流控（适配DC-A568-V06串口4）
bool configureSerial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口参数获取失败");
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
        perror("串口参数配置失败");
        return false;
    }
    return true;
}

// 初始化串口（固定为/dev/ttyS4）
bool initSerial() {
    const char *port = "/dev/ttyS4";
    serialFd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror("串口打开失败（必须用sudo运行）");
        return false;
    }
    if (!configureSerial(serialFd)) {
        close(serialFd);
        serialFd = -1;
        return false;
    }
    cout << "串口" << port << "初始化成功" << endl;
    return true;
}

// 核心功能：读取温湿度（新增完整报文打印）
void readHumiture() {
    if (serialFd < 0) return;

    // 发送报文：从机0x01、寄存器0x0000、读2个寄存器（湿度+温度）
    unsigned char sendBuf[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    write(serialFd, sendBuf, sizeof(sendBuf));
    cout << "发送读取指令：01 03 00 00 00 02 C4 0B" << endl;

    // 接收传感器响应
    unsigned char recvBuf[256] = {0};
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));

    // 打印完整接收报文（无论成功/失败都显示）
    cout << "接收完整报文（长度：" << len << "字节）：";
    for (ssize_t i = 0; i < len; ++i) {
        printf("%02X ", recvBuf[i]);
    }
    cout << endl;

    // 响应长度9字节（符合MODBUS RTU协议）
    if (len == 9 && recvBuf[0] == 0x01 && recvBuf[1] == 0x03) {
        // 解析数据（湿度在前，温度在后，大端序）
        uint16_t humiData = (recvBuf[3] << 8) | recvBuf[4];
        uint16_t tempData = (recvBuf[5] << 8) | recvBuf[6];
        float temperature = tempData / 10.0;
        float humidity = humiData / 10.0;

        cout << "解析成功！" << endl;
        cout << "温度：" << fixed << setprecision(1) << temperature << "℃ " 
             << "湿度：" << fixed << setprecision(1) << humidity << "%RH" << endl;
    } else if (len == 0) {
        cout << "未收到响应（检查接线、传感器地址）" << endl;
    } else {
        cout << "响应异常（地址/功能码不匹配或格式错误）" << endl;
    }
    cout << "----------------------------------------" << endl;
}

// 关闭串口
void closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        cout << "串口已关闭" << endl;
    }
}

int main() {
    InitEncoding();

    if (!initSerial()) return 1;

    cout << "开始实时温湿度监控（按Ctrl+C停止）" << endl;
    cout << "----------------------------------------" << endl;

    while (true) {
        readHumiture();
        this_thread::sleep_for(chrono::seconds(1));  // 1秒读取一次
    }

    closeSerial();
    return 0;
}