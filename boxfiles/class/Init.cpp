#include "Init.h"
 
// 全局变量：串口文件描述符（供电磁阀函数使用）
int SerialPortStutas = -1;
// 传感器GPIO路径（可根据实际硬件修改）
const char* gpiopath = "/sys/class/gpio/gpio33/value";
const char* portname = "/dev/ttyS4";

//编码初始化（解决中文乱码）
void Init::InitEncoding() {
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
        perror("设置编码失败");
    }
}
//串口配置：9600波特率、8N1、无流控（适配DC-A568-V06串口4）
bool Init::ConfigureSerial(int fd) {
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
//整体的基础设置初始化
void Init::Init(const string& portname, const string& gpiopath) :
    SerialPortStutas(-1),
    GPIOPath(gpiopath),
    PortName(portname) {}
//串口初始化完成
void Init::~Init() {
    cout << "初始化完成" << endl;
}
//初始化串口（固定为/dev/ttyS4）（初始化函数，需在调用开关函数前执行）
bool Init::InitSerial(const char *PortName) {
    SerialPortStutas = open(PortName, O_RDWR | O_NOCTTY | O_SYNC);
    if (SerialPortStutas < 0) {
        perror("open serial port");
        return false;
    }

    if (!configureSerial(SerialPortStutas)) {
        close(SerialPortStutas);
        SerialPortStutas = -1;
        return false;
    }

    cout << "Serial port initialized successfully" << endl;
    return true;
}
//关闭串口（程序结束时调用）
void Init::CloseSerial() {
    if (SerialPortStutas >= 0) {
        close(SerialPortStutas);
        SerialPortStutas = -1;
        cout << "Serial port closed" << endl;
    }
}
