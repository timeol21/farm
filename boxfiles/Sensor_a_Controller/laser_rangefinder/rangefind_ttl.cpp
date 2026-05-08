#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <cstdlib>

// ==================== 配置 ====================
const char* SERIAL_PORT = "/dev/ttyACM0";
const int BAUDRATE = B115200;
// ==============================================

int initSerialPort() {
    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
    if (fd == -1) return -1;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);

    cfsetospeed(&tty, BAUDRATE);
    cfsetispeed(&tty, BAUDRATE);

    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

// 1. 必须先发：自动波特率 0x55
void sendAutoBaud(int fd) {
    unsigned char c = 0x55;
    write(fd, &c, 1);
    fsync(fd);
    std::cout << "✅ 发送 0x55 自动波特率\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    tcflush(fd, TCIOFLUSH);
}

// 2. 必须主动发：测距指令
void sendMeasureCmd(int fd) {
    // 你确认成功的指令
    unsigned char cmd[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23};
    write(fd, cmd, sizeof(cmd));
    fsync(fd);

    std::cout << "📤 发送测距指令: ";
    for (int i=0; i<sizeof(cmd); i++) printf("%02X ", cmd[i]);
    std::cout << "\n";

    // 模块必须等待测量
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
}

// 3. 读取并解析返回
void readAndParse(int fd) {
    unsigned char buf[512];
    memset(buf, 0, 512);
    int len = read(fd, buf, 512);

    if (len <= 0) {
        std::cerr << "❌ 无返回\n";
        return;
    }

    // 打印原始数据
    std::cout << "📥 返回: ";
    for (int i=0; i<len; i++) printf("%02X ", buf[i]);
    std::cout << "\n";

    // 正确帧：13字节 AA ... 22
    if (len >=13 && buf[0] == 0xAA && buf[3] == 0x22) {
        int dist = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
        std::cout << "========================================\n";
        std::cout << "✅ 距离 = " << dist/1000.0 << " m\n";
        std::cout << "========================================\n\n";
    }
}

int main() {
    system(("chmod 666 " + std::string(SERIAL_PORT)).c_str());
    int fd = initSerialPort();
    if (fd < 0) return 1;

    // ======================
    // 🔥 核心流程：必须按这个顺序
    // ======================
    sendAutoBaud(fd);   // 1. 先发0x55
    std::cout << "✅ 准备开始测距\n\n";

    while (true) {
        tcflush(fd, TCIOFLUSH);
        sendMeasureCmd(fd);  // 2. 主动发测距指令
        readAndParse(fd);    // 3. 读取结果
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    close(fd);
    return 0;
}


// #include <iostream>       //只有FF返回
// #include <fcntl.h>
// #include <termios.h>
// #include <unistd.h>
// #include <cstring>
// #include <chrono>
// #include <thread>

// // ==================== 只改这里 ====================
// const char* SERIAL_PORT = "/dev/ttyS4";
// const int BAUDRATE = B115200;
// // ==================================================

// int initSerialPort() {
//     int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
//     if (fd == -1) return -1;

//     struct termios tty;
//     memset(&tty, 0, sizeof(tty));
//     tcgetattr(fd, &tty);

//     cfsetospeed(&tty, BAUDRATE);
//     cfsetispeed(&tty, BAUDRATE);

//     tty.c_cflag = CS8 | CREAD | CLOCAL;
//     tty.c_iflag = 0;
//     tty.c_oflag = 0;
//     tty.c_lflag = 0;
//     tty.c_cc[VMIN] = 0;
//     tty.c_cc[VTIME] = 5;

//     tcsetattr(fd, TCSANOW, &tty);
//     tcflush(fd, TCIOFLUSH);
//     return fd;
// }

// // 解析 JRT 标准帧：AA ... 22 ... 13字节
// void parseAndPrint(const unsigned char* buf, int len) {
//     if (len < 13 || buf[0] != 0xAA || buf[3] != 0x22) return;

//     int dist = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
//     std::cout << "✅ 实时距离 = " << dist/1000.0 << " m\n";
// }

// int main() {
//     system(("chmod 666 " + std::string(SERIAL_PORT)).c_str());
//     int fd = initSerialPort();
//     if (fd < 0) return 1;

//     std::cout << "✅ 开始读取激光数据（模块自动输出）\n\n";

//     unsigned char buf[512];
//     while (true) {
//         memset(buf, 0, 512);
//         int len = read(fd, buf, 512);
        
//         if (len > 0) {
//             // 打印原始数据（你会看到正确帧）
//             for(int i=0;i<len;i++) printf("%02X ", buf[i]);
//             std::cout << "\n";
            
//             parseAndPrint(buf, len);
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(50));
//     }

//     close(fd);
//     return 0;
// }
// #include <iostream>            //只有乱码返回
// #include <fcntl.h>
// #include <termios.h>
// #include <unistd.h>
// #include <cstring>
// #include <iomanip>
// #include <chrono>
// #include <thread>
// #include <cstdlib>

// // ==================== 核心配置 ====================
// const char* SERIAL_PORT = "/dev/ttyS4";
// const int BAUDRATE = B115200;
// const int BUFFER_SIZE = 512;
// const int MEASURE_DELAY_MS = 600;    // 延长到 600ms，保证模块返回
// const int READ_TIMEOUT_MS = 1000;
// // ==================================================

// // 串口赋权
// bool grantSerialPortPermission() {
//     std::string chmod_cmd = "chmod 666 " + std::string(SERIAL_PORT);
//     system(chmod_cmd.c_str());
//     std::cout << "✅ 串口权限已配置: " << SERIAL_PORT << std::endl;
//     return true;
// }

// // TTL 串口初始化（修复版）
// int initSerialPort() {
//     int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);
//     if (fd == -1) {
//         std::cerr << "❌ 打开串口失败" << std::endl;
//         return -1;
//     }

//     struct termios tty;
//     memset(&tty, 0, sizeof(tty));
//     tcgetattr(fd, &tty);

//     cfsetospeed(&tty, BAUDRATE);
//     cfsetispeed(&tty, BAUDRATE);

//     tty.c_cflag &= ~PARENB;
//     tty.c_cflag &= ~CSTOPB;
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;
//     tty.c_cflag &= ~CRTSCTS;
//     tty.c_cflag |= CREAD | CLOCAL;

//     tty.c_iflag = 0;
//     tty.c_oflag = 0;
//     tty.c_lflag = 0;

//     // 关键修复：阻塞读取，等待完整数据
//     tty.c_cc[VMIN] = 13;   // 等待至少13字节
//     tty.c_cc[VTIME] = 50;  // 5秒超时

//     tcsetattr(fd, TCSANOW, &tty);
//     tcflush(fd, TCIOFLUSH);
//     std::cout << "✅ 串口初始化完成 115200 8N1" << std::endl;
//     return fd;
// }

// // 发送测量指令
// void sendMeasureCmd(int fd) {
//     unsigned char cmd[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23};

//     std::cout << "\n📤 发送指令: ";
//     for (int i=0; i<sizeof(cmd); i++)
//         printf("%02X ", cmd[i]);

//     write(fd, cmd, sizeof(cmd));
//     fsync(fd);  // 强制发送

//     // 必须等模块返回数据
//     std::this_thread::sleep_for(std::chrono::milliseconds(MEASURE_DELAY_MS));
// }

// // 稳定读取（修复版）
// int readResponse(int fd, unsigned char* buf) {
//     memset(buf, 0, BUFFER_SIZE);
//     int total = read(fd, buf, BUFFER_SIZE);

//     if (total <= 0) {
//         std::cerr << " ❌ 读取超时/无数据" << std::endl;
//         return -1;
//     }

//     std::cout << "\n📥 接收: ";
//     for (int i=0; i<total; i++)
//         printf("%02X ", buf[i]);
//     std::cout << " len=" << total << std::endl;

//     return total;
// }

// // 解析距离
// bool parseDistance(const unsigned char* buf, int len) {
//     if (len < 13 || buf[0] != 0xAA || buf[3] != 0x22) {
//         std::cerr << "⚠️  帧格式错误" << std::endl;
//         return false;
//     }

//     int dist = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
//     std::cout << "========================================" << std::endl;
//     std::cout << "✅ 距离 = " << dist << " mm → " << dist/1000.0 << " m" << std::endl;
//     std::cout << "========================================" << std::endl;
//     return true;
// }

// int main() {
//     grantSerialPortPermission();
//     int fd = initSerialPort();
//     if (fd < 0) return 1;

//     unsigned char buf[BUFFER_SIZE];
//     while (true) {
//         tcflush(fd, TCIOFLUSH);  // 清空缓冲区
//         sendMeasureCmd(fd);
//         int len = readResponse(fd, buf);
//         if (len > 0) parseDistance(buf, len);
//         std::this_thread::sleep_for(std::chrono::milliseconds(300));
//     }

//     close(fd);
//     return 0;
// }
// ztl@RK356X:~/program/boxfiles/Sensor_a_Controller/laser_rangefinder$ g++ rangefind_ttl.cpp -o rangefind_ttl
// ztl@RK356X:~/program/boxfiles/Sensor_a_Controller/laser_rangefinder$ sudo ./rangefind_ttl
// ✅ 串口权限已配置: /dev/ttyS4
// ✅ 串口初始化完成 115200 8N1

// 📤 发送指令: AA 00 00 20 00 01 00 02 23 
// 📥 接收: FF  len=1
// ⚠️  帧格式错误

// 📤 发送指令: AA 00 00 20 00 01 00 02 23 
// 📥 接收: FF FF FF FF F7 FF FF F7 FF  len=9
// ⚠️  帧格式错误

// 📤 发送指令: AA 00 00 20 00 01 00 02 23 
// 📥 接收: 7F FF FF FF DE FF FF FF FF 79 FF CF FF FE FF FF FB 5F FF FF FF FF FB 7F 6F 7F FA FF EF 7F FF FF FF 7B CF FF FF BF F7 EF FF FF F6 7F C6 7E FF FF FF 7A DF FE FB FF FF FF FB 6B EB FB 53 FF EF 7E FF FF 7F FF 7F FF FF FF 7F 77 EF EF DE 7F FF FF FF FF  len=82
// ⚠️  帧格式错误

// 📤 发送指令: AA 00 00 20 00 01 00 02 23  ❌ 读取超时/无数据

// 📤 发送指令: AA 00 00 20 00 01 00 02 23  ❌ 读取超时/无数据
// ^C
// ztl@RK356X:~/program/boxfiles/Sensor_a_Controller/laser_rangefinder$ 


// #include <iostream>           //激光在闪，但是没有数据返回
// #include <fcntl.h>
// #include <termios.h>
// #include <unistd.h>
// #include <cstring>
// #include <iomanip>
// #include <chrono>
// #include <thread>
// #include <cstdlib>

// // ==================== 核心配置 ====================
// const char* SERIAL_PORT = "/dev/ttyS4";   // 你的TTL端口
// const int BAUDRATE = B115200;             // 激光模块固定115200
// const int BUFFER_SIZE = 512;
// const int MEASURE_DELAY_MS = 500;          // TTL模式必须≥300ms
// const int READ_TIMEOUT_MS = 800;           // 超时时间放宽
// // ==================================================

// // 串口赋权
// bool grantSerialPortPermission() {
//     std::string chmod_cmd = "chmod 666 " + std::string(SERIAL_PORT);
//     system(chmod_cmd.c_str());
//     std::cout << "✅ 串口权限已配置: " << SERIAL_PORT << std::endl;
//     return true;
// }

// // TTL 串口初始化（纯串口，无485切换）
// int initSerialPort() {
//     int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
//     if (fd == -1) {
//         std::cerr << "❌ 打开串口失败" << std::endl;
//         return -1;
//     }

//     struct termios tty;
//     memset(&tty, 0, sizeof(tty));
//     tcgetattr(fd, &tty);

//     cfsetospeed(&tty, BAUDRATE);
//     cfsetispeed(&tty, BAUDRATE);

//     tty.c_cflag &= ~PARENB;
//     tty.c_cflag &= ~CSTOPB;
//     tty.c_cflag &= ~CSIZE;
//     tty.c_cflag |= CS8;
//     tty.c_cflag &= ~CRTSCTS;
//     tty.c_cflag |= CREAD | CLOCAL;

//     tty.c_iflag = 0;
//     tty.c_oflag = 0;
//     tty.c_lflag = 0;

//     tty.c_cc[VMIN] = 0;
//     tty.c_cc[VTIME] = 10;  // 100ms

//     tcsetattr(fd, TCSANOW, &tty);
//     tcflush(fd, TCIOFLUSH);  // 关键：清空缓冲区

//     std::cout << "✅ 串口初始化完成 115200 8N1" << std::endl;
//     return fd;
// }

// // 发送单次测量指令（你成功的指令）
// void sendMeasureCmd(int fd) {
//     unsigned char cmd[] = {0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x02, 0x23};

//     std::cout << "\n📤 发送指令: ";
//     for (int i=0; i<sizeof(cmd); i++)
//         printf("%02X ", cmd[i]);

//     write(fd, cmd, sizeof(cmd));

//     // TTL 必须等待模块测量完成
//     std::this_thread::sleep_for(std::chrono::milliseconds(MEASURE_DELAY_MS));
// }

// // 稳定读取（TTL专用）
// int readResponse(int fd, unsigned char* buf) {
//     memset(buf, 0, BUFFER_SIZE);
//     int total = 0;
//     auto start = std::chrono::steady_clock::now();

//     while (true) {
//         int len = read(fd, buf + total, BUFFER_SIZE - total);
//         if (len > 0) total += len;

//         // 读到完整13字节帧就退出
//         if (total >= 13 && buf[0] == 0xAA && buf[3] == 0x22)
//             break;

//         // 超时
//         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
//             std::chrono::steady_clock::now() - start).count();
//         if (elapsed > READ_TIMEOUT_MS) break;
//     }

//     if (total <= 0) {
//         std::cerr << "❌ 读取超时" << std::endl;
//         return -1;
//     }

//     // 打印接收
//     std::cout << "\n📥 接收数据: ";
//     for (int i=0; i<total; i++)
//         printf("%02X ", buf[i]);
//     std::cout << "len=" << total << std::endl;

//     return total;
// }

// // 解析距离（你成功的解析方式）
// bool parseDistance(const unsigned char* buf, int len) {
//     if (len < 13 || buf[0] != 0xAA || buf[3] != 0x22)
//         return false;

//     int dist = (buf[6]<<24) | (buf[7]<<16) | (buf[8]<<8) | buf[9];
//     std::cout << "========================================" << std::endl;
//     std::cout << "✅ 距离 = " << dist << " mm → " << dist/1000.0 << " m" << std::endl;
//     std::cout << "========================================" << std::endl;
//     return true;
// }

// int main() {
//     grantSerialPortPermission();
//     int fd = initSerialPort();
//     if (fd < 0) return 1;

//     unsigned char buf[BUFFER_SIZE];
//     while (true) {
//         tcflush(fd, TCIOFLUSH);  // 每次循环清空缓冲区（关键！）
//         sendMeasureCmd(fd);
//         int len = readResponse(fd, buf);
//         if (len > 0) parseDistance(buf, len);
//         std::this_thread::sleep_for(std::chrono::milliseconds(200));
//     }

//     close(fd);
//     return 0;
// }

// #include <iostream>
// #include <fcntl.h>
// #include <termios.h>
// #include <unistd.h>
// #include <cstring>
// #include <chrono>
// #include <thread>
// using namespace std;

// const char* PORT = "/dev/ttyS1";

// int initSerial() {
//     int fd = open(PORT, O_RDWR | O_NOCTTY);
//     if (fd < 0) return -1;

//     struct termios tty;
//     tcgetattr(fd, &tty);
//     cfsetospeed(&tty, B115200);
//     cfsetispeed(&tty, B115200);

//     tty.c_cflag = CS8 | CREAD | CLOCAL;
//     tty.c_iflag = 0;
//     tty.c_oflag = 0;
//     tty.c_lflag = 0;
//     tty.c_cc[VMIN] = 0;
//     tty.c_cc[VTIME] = 50;

//     tcsetattr(fd, TCSANOW, &tty);
//     tcflush(fd, TCIOFLUSH);
//     return fd;
// }

// int main() {
//     int fd = initSerial();
//     if (fd < 0) { cout << "打开失败\n"; return 1; }

//     cout << "✅ TTL 串口就绪，发送 ASCII 测量指令\n\n";

//     while (1) {
//         // ✅ ✅ ✅ 终极指令：ASCII 模式，单次测量
//         const char* cmd = "M\n";
//         write(fd, cmd, strlen(cmd));

//         cout << "📤 发送: M\\n  | 等待响应...";

//         // 读取
//         char buf[128] = {0};
//         int len = read(fd, buf, 127);
//         if (len > 0) {
//             cout << "\r📥 收到: " << buf;
//         } else {
//             cout << "\r❌ 超时";
//         }

//         cout << endl;
//         this_thread::sleep_for(chrono::milliseconds(500));
//         tcflush(fd, TCIOFLUSH);
//     }

//     close(fd);
//     return 0;
// }