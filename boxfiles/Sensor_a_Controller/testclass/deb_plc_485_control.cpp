#include <iostream>  //黄yx写的两路plc的控制，他说成功
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

using namespace std;

// 全局变量：两个PLC串口文件描述符（供所有函数使用）
int plc1Fd = -1;
int plc2Fd = -1;

// 编码初始化函数（与烟雾检测器代码完全一致）
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

// 串口配置函数（与烟雾检测器代码完全一致）
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

// 初始化串口（支持指定串口路径，适配PLC1/PLC2）
bool initSerial(const char *portName) {
    int fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("open serial port");
        return false;
    }

    if (!configureSerial(fd)) {
        close(fd);
        return false;
    }

    cout << "Serial port initialized successfully (port: " << portName << ")" << endl;
    return true;
}

// 发送PLC指令（延长发送时间，与原逻辑一致，适配新风格）
bool sendPlcCmd(int fd, const char* plcName, const unsigned char* sendBuf, int len) {
    if (fd < 0) {
        cerr << "Error: " << plcName << " serial port not initialized" << endl;
        return false;
    }

    // 连续发送5次（提高指令接收成功率）
    for (int i = 0; i < 5; i++) {
        ssize_t sent = write(fd, sendBuf, len);
        if (sent != (ssize_t)len) {
            perror(("write (" + string(plcName) + " command)").c_str());
            return false;
        }

        // 打印发送的报文（十六进制格式）
        cout << plcName << " send " << i + 1 << " times - Command: ";
        for (size_t j = 0; j < len; ++j)
            printf("%02X ", sendBuf[j]);
        cout << endl;

        tcdrain(fd); // 等待发送完成
        this_thread::sleep_for(chrono::microseconds(200000)); // 延时200ms（替代usleep）
    }

    cout << plcName << " command sent completely" << endl;
    return true;
}

// 关闭串口（关闭PLC1和PLC2的串口）
void closeSerial() {
    if (plc1Fd >= 0) {
        close(plc1Fd);
        plc1Fd = -1;
        cout << "PLC1 serial port closed" << endl;
    }
    if (plc2Fd >= 0) {
        close(plc2Fd);
        plc2Fd = -1;
        cout << "PLC2 serial port closed" << endl;
    }
}

// 主函数：控制PLC1/PLC2的Y0口依次开关
int main() {
    // 初始化编码
    InitEncoding();

    // 串口路径（保持原参数）
    const char* plc1Port = "/dev/ttyS4";
    const char* plc2Port = "/dev/ttyUSB10";

    // 初始化PLC1串口
    plc1Fd = open(plc1Port, O_RDWR | O_NOCTTY | O_SYNC);
    if (plc1Fd < 0) {
        perror("open PLC1 serial port");
        return 1;
    }
    if (!configureSerial(plc1Fd)) {
        closeSerial();
        return 1;
    }

    // 初始化PLC2串口
    plc2Fd = open(plc2Port, O_RDWR | O_NOCTTY | O_SYNC);
    if (plc2Fd < 0) {
        perror("open PLC2 serial port");
        closeSerial();
        return 1;
    }
    if (!configureSerial(plc2Fd)) {
        closeSerial();
        return 1;
    }

    cout << "=== PLC Y0 Control Started ===" << endl;
    cout << "PLC1 Port: /dev/ttyS4 | PLC2 Port: /dev/ttyUSB10" << endl;
    cout << "Baudrate: 9600 | Protocol: MODBUS RTU (Function Code 05)" << endl;
    cout << "=============================================" << endl << endl;

    // PLC指令定义（MODBUS RTU 写单个线圈 05）
    // PLC1 Y0 打开：01(地址) 05(功能码) 0500(Y0地址) FF00(开启) 8CF6(CRC)
    unsigned char plc1Y0On[]  = {0x01,0x05,0x05,0x00,0xFF,0x00,0x8C,0xF6};
    // PLC1 Y0 关闭：01(地址) 05(功能码) 0500(Y0地址) 0000(关闭) CD06(CRC)
    unsigned char plc1Y0Off[] = {0x01,0x05,0x05,0x00,0x00,0x00,0xCD,0x06};
    // PLC2 Y0 打开：02(地址) 05(功能码) 0500(Y0地址) FF00(开启) 8CC5(CRC)
    unsigned char plc2Y0On[]  = {0x02,0x05,0x05,0x00,0xFF,0x00,0x8C,0xC5};
    // PLC2 Y0 关闭：02(地址) 05(功能码) 0500(Y0地址) 0000(关闭) CD35(CRC)
    unsigned char plc2Y0Off[] = {0x02,0x05,0x05,0x00,0x00,0x00,0xCD,0x35};

    // 执行PLC控制逻辑
    cout << "===== Open PLC1 Y0 =====" << endl;
    sendPlcCmd(plc1Fd, "PLC1", plc1Y0On, sizeof(plc1Y0On));
    this_thread::sleep_for(chrono::seconds(3)); // 延时3秒（替代sleep）
    cout << "---------------------------------------------" << endl;

    cout << "===== Open PLC2 Y0 =====" << endl;
    sendPlcCmd(plc2Fd, "PLC2", plc2Y0On, sizeof(plc2Y0On));
    this_thread::sleep_for(chrono::seconds(3));
    cout << "---------------------------------------------" << endl;

    cout << "===== Close PLC1 Y0 =====" << endl;
    sendPlcCmd(plc1Fd, "PLC1", plc1Y0Off, sizeof(plc1Y0Off));
    this_thread::sleep_for(chrono::seconds(3));
    cout << "---------------------------------------------" << endl;

    cout << "===== Close PLC2 Y0 =====" << endl;
    sendPlcCmd(plc2Fd, "PLC2", plc2Y0Off, sizeof(plc2Y0Off));
    cout << "---------------------------------------------" << endl;

    // 关闭串口
    closeSerial();
    cout << endl << "=== PLC Y0 Control Completed ===" << endl;

    return 0;
}