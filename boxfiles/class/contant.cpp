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

using namespace std;

// 全局变量：串口文件描述符（供电磁阀函数使用）
int serialFd = -1;
// 传感器GPIO路径（可根据实际硬件修改）
const char* gpio_value_path = "/sys/class/gpio/gpio33/value";

// -------------------------- 编码初始化（解决中文乱码） --------------------------
void initEncoding() {
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
        perror("设置编码失败");
    }
}

// -------------------------- 串口配置相关函数 --------------------------
bool configureSerial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    // 设置波特率9600
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    // 8数据位、无校验、1停止位、禁用流控
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

    // 读取超时（1秒）
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("串口参数设置失败（tcsetattr）");
        return false;
    }
    return true;
}

bool initSerial(const char *portName = "/dev/ttyS4") {
    if (serialFd >= 0) {
        cout << "串口已处于初始化状态" << endl;
        return true;
    }

    serialFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror("串口打开失败");
        return false;
    }

    if (!configureSerial(serialFd)) {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    cout << "串口初始化成功（设备：" << portName << "）" << endl;
    return true;
}

void closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        cout << "串口已关闭" << endl;
    }
}

// -------------------------- 电磁阀控制函数 --------------------------
bool openSolenoidValve() {
    if (serialFd < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0xFF, 0x00, 0x8C, 0xF6};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀开启指令发送失败");
        return false;
    }

    cout << "电磁阀开启指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收响应
    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀状态：已开启" << endl;
    return true;
}

bool closeSolenoidValve() {
    if (serialFd < 0) {
        cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << endl;
        return false;
    }

    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x06};
    ssize_t sent = write(serialFd, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀关闭指令发送失败");
        return false;
    }

    cout << "电磁阀关闭指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    cout << endl;

    // 接收响应
    unsigned char recvBuf[256];
    ssize_t len = read(serialFd, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        cout << endl;
    }

    cout << "电磁阀状态：已关闭" << endl;
    return true;
}

// -------------------------- 传感器检测函数 --------------------------
void detectSensorStatus() {
    cout << "传感器检测已启动（每0.5秒读取一次），按'q'并回车退出检测" << endl;
    cout << "------------------------------------------------" << endl;

    while (true) {
        // 非阻塞监听键盘输入
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval tv = {0, 100000}; // 等待100ms

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            char c;
            cin >> c;
            if (c == 'q' || c == 'Q') {
                cout << "------------------------------------------------" << endl;
                cout << "传感器检测已退出" << endl;
                return;
            } else {
                cout << "无效输入，按'q'并回车退出检测" << endl;
            }
        }

        // 读取GPIO值
        std::ifstream gpio_file(gpio_value_path);
        if (!gpio_file.is_open()) {
            cerr << "传感器GPIO文件打开失败（检查路径或权限）" << endl;
            usleep(500000);
            continue;
        }

        int value;
        gpio_file >> value;

        // 输出传感器状态
        if (value == 0) {
            cout << "传感器状态：ON（低电平）" << endl;
        } else if (value == 1) {
            cout << "传感器状态：OFF（高电平）" << endl;
        } else {
            cout << "传感器状态：未知（数值：" << value << "）" << endl;
        }

        gpio_file.close();
        usleep(500000); // 0.5秒读取一次
    }
}

// -------------------------- 主菜单打印函数（新增） --------------------------
void printMainMenu() {
    cout << "\n=================================================" << endl;
    cout << "            设备控制中心（V1.0）" << endl;
    cout << "=================================================" << endl;
    cout << "功能选择：" << endl;
    cout << "  1 - 电磁阀控制（开启/关闭）" << endl;
    cout << "  2 - 传感器状态检测" << endl;
    cout << "  0 - 退出系统" << endl;
    cout << "=================================================" << endl;
}

// -------------------------- 主菜单交互逻辑 --------------------------
int main() {
    initEncoding(); // 初始化UTF-8编码，解决中文乱码

    int mainChoice;
    while (true) {
        // 每次循环都打印主菜单（关键修改）
        printMainMenu();

        cout << "请输入功能编号（0-2）：";
        cin >> mainChoice;

        switch (mainChoice) {
            case 1: {
                // 电磁阀控制子菜单
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
                        break; // 退出子菜单，回到主循环（会重新打印主菜单）
                    }

                    // 串口未初始化时自动尝试初始化
                    if (serialFd < 0) {
                        cout << "正在初始化串口..." << endl;
                        if (!initSerial()) {
                            cout << "串口初始化失败，无法进行电磁阀控制" << endl;
                            break;
                        }
                    }

                    switch (valveChoice) {
                        case 1:
                            openSolenoidValve();
                            break;
                        case 2:
                            closeSolenoidValve();
                            break;
                        default:
                            cout << "无效指令，请输入0-2之间的数字" << endl;
                            break;
                    }
                }
                break;
            }

            case 2:
                // 传感器检测（退出后返回主循环，重新打印主菜单）
                detectSensorStatus();
                break;

            case 0:
                // 退出系统
                cout << "\n正在退出系统，释放资源..." << endl;
                closeSerial();
                cout << "系统已退出，感谢使用！" << endl;
                return 0;

            default:
                cout << "无效功能编号，请输入0-2之间的数字" << endl;
                break;
        }
    }
}