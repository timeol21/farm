// //control_y0.cpp    //可以完成单次的开关控制
// #include <iostream>
// #include <fcntl.h>
// #include <unistd.h>
// #include <termios.h>
// #include <cstring>
// #include <chrono>
// #include <thread>

// using namespace std;

// bool configureSerial(int fd) {    //配置串口参数
//     struct termios tty;
//     if (tcgetattr(fd, &tty) != 0) {
//         perror("tcgetattr");
//         return false;
//     }

//     // 设置波特率 9600
//     cfsetospeed(&tty, B9600);
//     cfsetispeed(&tty, B9600);

//     // 控制模式：8数据位，无校验，1停止位
//     tty.c_cflag &= ~PARENB; // 无校验
//     tty.c_cflag &= ~CSTOPB; // 1停止位
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;     // 8位数据

//     tty.c_cflag &= ~CRTSCTS; // 禁用硬件流控
//     tty.c_cflag |= CREAD | CLOCAL; // 使能接收，忽略调制解调器状态线

//     // 输入模式设置
//     tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
//     tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

//     // 输出模式
//     tty.c_oflag = 0;

//     // 本地模式
//     tty.c_lflag = 0; // 非规范模式

//     // 读取时设置
//     tty.c_cc[VMIN] = 0;    // 非阻塞读取
//     tty.c_cc[VTIME] = 10;  // 读取超时1秒(单位为0.1秒)

//     if (tcsetattr(fd, TCSANOW, &tty) != 0) {
//         perror("tcsetattr");
//         return false;
//     }

//     return true;
// }

// int main() {    
//     //打开串口
//     const char *portName = "/dev/ttyS4";   //串口名称
//     int fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
//     if (fd < 0) {
//         perror("open serial port");
//         return 1;
//     }

//     if (!configureSerial(fd)) {
//         close(fd);
//         return 1;
//     }

//     // 固定发送报文 (十六进制)    //每 1 秒发送固定的 8 字节十六进制报文
//     // unsigned char sendBuf[] = {0x01, 0x01, 0x04, 0x00, 0x00, 0x01, 0xFC, 0xFA};  //“读线圈寄存器” 指令（功能码 0x01）,读取到地址 0x0004 的线圈状态为断开
//     // unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};  //Y0置ON报文,（电磁阀导通）
//     unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06}; //Y0置OFF报文，（电磁阀断开） 
    
// //依次为从机地址，功能码，连着两位的作为X0的MODBUS地址（十六进制地址），最后两位是CRC检验码
//     while (true) {
//         // 发送数据
//         ssize_t sent = write(fd, sendBuf, sizeof(sendBuf));
//         if (sent != (ssize_t)sizeof(sendBuf)) {
//             perror("write");
//             break;
//         }
//         cout << "Sent: ";
//         for (size_t i = 0; i < sizeof(sendBuf); ++i)
//             printf("%02X ", sendBuf[i]);
//         cout << endl;

//         // 接收数据
//         unsigned char recvBuf[256];
//         ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
//         if (len < 0) {
//             perror("read");
//             break;
//         } else if (len == 0) {
//             cout << "No data received (timeout)" << endl;
//         } else {
//             cout << "Received (" << len << " bytes): ";
//             for (ssize_t i = 0; i < len; ++i)
//                 printf("%02X ", recvBuf[i]);
//             cout << endl;
//         }

//         // 等待1秒
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//     }

//     close(fd);
//     return 0;
// }


// #include <iostream>   //循环控制Y0开关检测（eg:检测电磁阀是否能正常工作）,子佳代码，可用
// #include <fcntl.h>
// #include <unistd.h>
// #include <termios.h>
// #include <cstring>
// #include <chrono>
// #include <thread>

// using namespace std;

// bool configureSerial(int fd) {
//     struct termios tty;                                
//     if (tcgetattr(fd, &tty) != 0) {
//         perror("tcgetattr");
//         return false;
//     }

//     // 设置波特率 9600
//     cfsetospeed(&tty, B9600);
//     cfsetispeed(&tty, B9600);

//     // 控制模式：8数据位，无校验，1停止位
//     tty.c_cflag &= ~PARENB; // 无校验
//     tty.c_cflag &= ~CSTOPB; // 1停止位
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;     // 8位数据

//     tty.c_cflag &= ~CRTSCTS; // 禁用硬件流控
//     tty.c_cflag |= CREAD | CLOCAL; // 使能接收，忽略调制解调器状态线

//     // 输入模式设置
//     tty.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控
//     tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

//     // 输出模式
//     tty.c_oflag = 0;

//     // 本地模式
//     tty.c_lflag = 0; // 非规范模式

//     // 读取时设置
//     tty.c_cc[VMIN] = 0;    // 非阻塞读取
//     tty.c_cc[VTIME] = 10;  // 读取超时1秒(单位为0.1秒)

//     if (tcsetattr(fd, TCSANOW, &tty) != 0) {
//         perror("tcsetattr");
//         return false;
//     }

//     return true;
// }

// int main() {
//     const char *portName = "/dev/ttyS4";
//     int fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
//     if (fd < 0) {
//         perror("open serial port");
//         return 1;
//     }

//     if (!configureSerial(fd)) {
//         close(fd);
//         return 1;
//     }

//     // 固定发送报文 (十六进制)       //第一位，从机地址,第二位，功能码,第三第四位，控制的线圈地址（十六进制）(0500),最后两位是CRC16校验码，通过SSCOM V5.13.1,计算得来
//     unsigned char sendBuf1[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};  //
//     unsigned char sendBuf2[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06};
//     int cnt=0;                 
//     while (cnt<10) {
//         cnt++;
//         // 发送数据
//         ssize_t sent = write(fd, sendBuf1, sizeof(sendBuf1));
//         if (sent != (ssize_t)sizeof(sendBuf1)) {
//             perror("write");
//             break;
//         }
//         cout << "Sent: ";
//         for (size_t i = 0; i < sizeof(sendBuf1); ++i)
//             printf("%02X ", sendBuf1[i]);
//         cout << endl;

//         // 接收数据
//         unsigned char recvBuf[256];
//         ssize_t len = read(fd, recvBuf, sizeof(recvBuf));
//         if (len < 0) {
//             perror("read");
//             break;
//         } else if (len == 0) {
//             cout << "No data received (timeout)" << endl;
//         } else {
//             cout << "Received (" << len << " bytes): ";
//             for (ssize_t i = 0; i < len; ++i)
//                 printf("%02X ", recvBuf[i]);
//             cout << endl;
//         }

//         std::this_thread::sleep_for(std::chrono::seconds(1));

//         // 发送数据
//         sent = write(fd, sendBuf2, sizeof(sendBuf2));
//         if (sent != (ssize_t)sizeof(sendBuf2)) {
//             perror("write");
//             break;
//         }
//         cout << "Sent: ";
//         for (size_t i = 0; i < sizeof(sendBuf2); ++i)
//             printf("%02X ", sendBuf2[i]);
//         cout << endl;

//         len = read(fd, recvBuf, sizeof(recvBuf));
//         if (len < 0) {
//             perror("read");
//             break;
//         } else if (len == 0) {
//             cout << "No data received (timeout)" << endl;
//         } else {
//             cout << "Received (" << len << " bytes): ";
//             for (ssize_t i = 0; i < len; ++i)
//                 printf("%02X ", recvBuf[i]);
//             cout << endl;
//         }

//         // 等待1秒
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//     }

//     close(fd);
//     return 0;
// }

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>

using namespace std;

// 全局变量存储串口文件描述符，供两个功能函数使用
int serialFd = -1;

// 配置串口参数（复用原有逻辑）
bool configureSerial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }

    // 设置波特率 9600
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    // 8数据位、无校验、1停止位
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // 输入输出模式配置
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    // 读取超时设置
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return false;
    }

    return true;
}

// 打开串口（初始化函数，需在调用开关函数前执行）
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

    cout << "Serial port initialized successfully" << endl;
    return true;
}

// 电磁阀开启函数
bool openSolenoidValve() {
    if (serialFd < 0) {
        cerr << "Error: Serial port not initialized" << endl;
        return false;
    }

    // Y0置ON报文（电磁阀导通）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("write (open valve)");
        return false;
    }

    cout << "Open valve command sent: ";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收响应（可选保留，如需忽略响应可删除此部分）
    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "Open valve response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    return true;
}

// 电磁阀关闭函数
bool closeSolenoidValve() {
    if (serialFd < 0) {
        cerr << "Error: Serial port not initialized" << endl;
        return false;
    }

    // Y0置OFF报文（电磁阀断开）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("write (close valve)");
        return false;
    }

    cout << "Close valve command sent: ";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收响应（可选保留，如需忽略响应可删除此部分）
    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "Close valve response (" << len << " bytes): ";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    return true;
}

// 关闭串口（程序结束时调用）
void closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        cout << "Serial port closed" << endl;
    }
}

// 测试示例（可根据需要删除）
int main() {
    // 初始化串口
    if (!initSerial()) {
        return 1;
    }

    // 调用开启函数
    openSolenoidValve();
    this_thread::sleep_for(chrono::seconds(2)); // 保持开启2秒

    // 调用关闭函数
    closeSolenoidValve();
    this_thread::sleep_for(chrono::seconds(2)); // 保持关闭2秒

    // 关闭串口
    closeSerial();

    return 0;
}