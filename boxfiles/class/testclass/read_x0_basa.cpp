#include <iostream>    //只有电磁阀可以正常使用，其他的功能暂时不可以正常的使用
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <thread>
#include <fstream>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>

using namespace std;

// 全局变量
int serialFd = -1;
const char* gpio_value_path = "/sys/class/gpio/gpio33/value";

// -------------------------- 基础功能：中文编码初始化（永久生效） --------------------------
void initEncoding() {
    // 强制设置终端为UTF-8编码，适配RK3568的Linux系统
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
        perror("中文编码设置失败");
    }
}

// -------------------------- 工具函数：CRC16校验（MODBUS RTU专用） --------------------------
uint16_t crc16Modbus(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return (crc << 8) | (crc >> 8); // 高低字节交换
}

// -------------------------- 串口配置函数（通用） --------------------------
bool configureSerial(int fd) {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    // 9600波特率、8N1配置
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // 输入输出优化
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // 1秒超时
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("串口参数生效失败（tcsetattr）");
        return false;
    }
    return true;
}

// -------------------------- 功能1：电磁阀控制相关 --------------------------
bool initSerialValve(const char *portName = "/dev/ttyS4") {
    if (serialFd >= 0) {
        cout << "电磁阀串口已初始化" << endl;
        return true;
    }
    serialFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror("电磁阀串口打开失败");
        return false;
    }
    if (!configureSerial(serialFd)) {
        close(serialFd);
        serialFd = -1;
        return false;
    }
    cout << "电磁阀串口初始化成功（设备：" << portName << "）" << endl;
    return true;
}

void closeSerialValve() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        cout << "电磁阀串口已关闭" << endl;
    }
}

bool openSolenoidValve() {
    if (serialFd < 0) {
        cerr << "错误：电磁阀串口未初始化" << endl;
        return false;
    }
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));
    if (sent != sizeof(sendBuf)) {
        perror("电磁阀开启指令发送失败");
        return false;
    }
    cout << "电磁阀开启指令：";
    for (size_t i = 0; i < sizeof(sendBuf); i++) printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; i++) printf("%02X ", recvBuf[i]);
        cout << endl;
    }
    cout << "电磁阀状态：已开启" << endl;
    return true;
}

bool closeSolenoidValve() {
    if (serialFd < 0) {
        cerr << "错误：电磁阀串口未初始化" << endl;
        return false;
    }
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));
    if (sent != sizeof(sendBuf)) {
        perror("电磁阀关闭指令发送失败");
        return false;
    }
    cout << "电磁阀关闭指令：";
    for (size_t i = 0; i < sizeof(sendBuf); i++) printf("%02X ", sendBuf[i]);
    cout << endl;

    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; i++) printf("%02X ", recvBuf[i]);
        cout << endl;
    }
    cout << "电磁阀状态：已关闭" << endl;
    return true;
}

// -------------------------- 功能2：传感器GPIO检测 --------------------------
void detectSensorStatus() {
    cout << "=== 传感器检测启动 ===" << endl;
    cout << "每0.5秒读取一次，按'q'回车退出" << endl;
    cout << "------------------------" << endl;

    while (true) {
        // 监听退出指令
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 100000};
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c;
            cin >> c;
            if (c == 'q' || c == 'Q') {
                cout << "------------------------" << endl;
                cout << "传感器检测已退出" << endl;
                return;
            } else {
                cout << "无效输入，按'q'回车退出" << endl;
            }
        }

        // 读取GPIO状态
        ifstream gpio_file(gpio_value_path);
        if (!gpio_file.is_open()) {
            cerr << "传感器GPIO打开失败（检查路径/权限）" << endl;
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

// -------------------------- 功能3：X0状态读取（无依赖库） --------------------------
int readX0Status(const char* serialPort = "/dev/ttyS4") {
    // 独立打开串口（不与电磁阀串口冲突）
    int fd = open(serialPort, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("X0读取串口打开失败");
        return -2;
    }

    if (!configureSerial(fd)) {
        close(fd);
        return -2;
    }

    // 构造MODBUS请求报文（功能码0x02，读取X0=地址1024=0x400）
    uint8_t reqBuf[8] = {0x01, 0x02, 0x04, 0x00, 0x00, 0x01};
    uint16_t crc = crc16Modbus(reqBuf, 6);
    reqBuf[6] = crc & 0xFF;
    reqBuf[7] = (crc >> 8) & 0xFF;

    // 发送报文
    ssize_t sent = write(fd, reqBuf, sizeof(reqBuf));
    if (sent != sizeof(reqBuf)) {
        perror("X0请求报文发送失败");
        close(fd);
        return -3;
    }
    cout << "X0请求报文：";
    for (size_t i = 0; i < sizeof(reqBuf); i++) printf("%02X ", reqBuf[i]);
    cout << endl;

    // 接收响应
    uint8_t respBuf[256] = {0};
    ssize_t recvLen = read(fd, respBuf, sizeof(respBuf));
    close(fd); // 立即关闭独立串口

    if (recvLen < 0) {
        perror("X0响应接收失败");
        return -4;
    } else if (recvLen == 0) {
        cout << "X0读取超时（PLC未响应）" << endl;
        return -5;
    }

    // 解析响应
    cout << "X0响应报文（" << recvLen << "字节）：";
    for (size_t i = 0; i < recvLen; i++) printf("%02X ", respBuf[i]);
    cout << endl;

    if (recvLen != 8 || respBuf[0] != 0x01 || respBuf[1] != 0x02 || respBuf[2] != 0x01) {
        cout << "X0响应格式错误" << endl;
        return -6;
    }

    return (respBuf[3] & 0x01) ? 1 : 0; // 提取X0状态
}

void readX0Loop() {
    cout << "=== X0状态读取启动 ===" << endl;
    cout << "每1秒读取一次，按'q'回车退出" << endl;
    cout << "------------------------" << endl;

    while (true) {
        // 监听退出指令
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 100000};
        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c;
            cin >> c;
            if (c == 'q' || c == 'Q') {
                cout << "------------------------" << endl;
                cout << "X0读取已退出" << endl;
                return;
            } else {
                cout << "无效输入，按'q'回车退出" << endl;
            }
        }

        // 读取并显示X0状态
        int status = readX0Status();
        switch (status) {
            case 1:
                cout << "X0状态：有信号输入（低电平有效）" << endl;
                break;
            case 0:
                cout << "X0状态：无信号输入" << endl;
                break;
            case -2:
                cout << "错误：串口操作失败" << endl;
                break;
            case -3:
                cout << "错误：报文发送失败" << endl;
                break;
            case -4:
                cout << "错误：响应接收失败" << endl;
                break;
            case -5:
                cout << "错误：PLC未响应" << endl;
                break;
            case -6:
                cout << "错误：响应格式错误" << endl;
                break;
            default:
                cout << "错误：未知状态码" << endl;
                break;
        }

        sleep(1);
    }
}

// -------------------------- 主菜单显示 --------------------------
void printMainMenu() {
    cout << "\n=================================================" << endl;
    cout << "            设备控制中心（V2.0）" << endl;
    cout << "=================================================" << endl;
    cout << "功能选择：" << endl;
    cout << "  1 - 电磁阀控制（开启/关闭）" << endl;
    cout << "  2 - GPIO传感器状态检测" << endl;
    cout << "  3 - PLC X0状态读取" << endl;
    cout << "  0 - 退出系统" << endl;
    cout << "=================================================" << endl;
}

// -------------------------- 主交互逻辑 --------------------------
int main() {
    initEncoding(); // 初始化中文编码（所有中文输出自动适配）

    int mainChoice;
    while (true) {
        printMainMenu();
        cout << "请输入功能编号（0-3）：";
        cin >> mainChoice;

        switch (mainChoice) {
            case 1: {
                // 电磁阀子菜单
                cout << "\n---------------- 电磁阀控制 ----------------" << endl;
                cout << "  1 - 开启电磁阀" << endl;
                cout << "  2 - 关闭电磁阀" << endl;
                cout << "  0 - 返回主菜单" << endl;
                cout << "--------------------------------------------" << endl;

                int valveChoice;
                while (true) {
                    cout << "请输入控制指令（0-2）：";
                    cin >> valveChoice;

                    if (valveChoice == 0) {
                        cout << "返回主菜单..." << endl;
                        break;
                    }

                    if (serialFd < 0) {
                        cout << "正在初始化电磁阀串口..." << endl;
                        if (!initSerialValve()) {
                            cout << "串口初始化失败，无法控制电磁阀" << endl;
                            break;
                        }
                    }

                    switch (valveChoice) {
                        case 1: openSolenoidValve(); break;
                        case 2: closeSolenoidValve(); break;
                        default: cout << "无效指令，请输入0-2" << endl; break;
                    }
                }
                break;
            }

            case 2:
                detectSensorStatus();
                break;

            case 3:
                readX0Loop();
                break;

            case 0:
                cout << "\n正在退出系统，释放资源..." << endl;
                closeSerialValve();
                cout << "系统已退出，感谢使用！" << endl;
                return 0;

            default:
                cout << "无效功能编号，请输入0-3" << endl;
                break;
        }
    }
}