#include "Init.h"
#include <iostream>
#include <cstring> 
#include <dirent.h>
#include <fstream>
#include <string>
#include <unistd.h>

 
// 全局变量：串口文件描述符（供电磁阀函数使用）
int SerialPortStutas = -1;
// 传感器GPIO路径（可根据实际硬件修改）
const char* gpio_value_path = "/sys/class/gpio/gpio33/value";
const char* portname = "/dev/ttyS4";
const char* gpio_export_path = "/sys/class/gpio/export";
const char* gpio_dir_path = "/sys/class/gpio/gpio33";

Init::Init(const string& portname, const string& gpio_value_path) :
    SerialPortStutas(-1),
    Gpio_Value_Path(gpio_value_path),
    PortName(portname),
    Gpio_Export_Path(gpio_export_path),
    Gpio_Dir_Path(gpio_dir_path) {}
//串口初始化完成
Init::~Init() {
}
void Init::InitEncoding() {
    // 方案1：优先尝试中文UTF-8编码（适配中文打印）
    const char* locales[] = {"zh_CN.UTF-8", "en_US.UTF-8", "C.UTF-8", "POSIX"};
    bool localeSet = false;

    // 依次尝试多个编码，确保能成功设置
    for (const char* loc : locales) {
        if (setlocale(LC_ALL, loc) != NULL) { // 用LC_ALL而非LC_CTYPE，覆盖所有编码场景
            cout << "编码初始化成功：使用 " << loc << " 编码" << endl;
            localeSet = true;
            break;
        }
        cerr << "尝试设置 " << loc << " 编码失败，继续尝试下一个..." << endl;
    }

    // 若所有编码都失败，给出明确提示（但不终止程序）
    if (!localeSet) {
        perror("所有编码设置均失败，可能导致打印乱码");
    }
}
//串口配置：9600波特率、8N1、无流控（适配DC-A568-V06串口4）,初始化 / 配置指定串口的通信参数
bool Init::ConfigureSerial(int fd) {//在调用.InitSerial的时候会调用这个函数
    struct termios tty;
    /*
    termios 是 Linux/Unix 系统中专门用于描述终端（包括串口）属性的结构体，
    里面包含了波特率、数据位、停止位、校验位、流控等所有串口通信的配置参数。
    */
    if (tcgetattr(fd, &tty) != 0) {//tcgetattr()成功时返回 0，失败时返回 -1
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
        /*
        tcsetattr()：这是 Linux/Unix 系统提供的系统调用，
        作用是设置（写入）指定文件描述符对应的终端 / 串口属性,
        TCSANOW 表示立即生效
        */
        perror("tcsetattr");
        return false;
    }

    return true;
}
//整体的基础设置初始化

//初始化串口（固定为/dev/ttyS4）（初始化函数，需在调用开关函数前执行）
bool Init::InitSerial() {
    SerialPortStutas = open(PortName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (SerialPortStutas < 0) {
        perror("open serial port");
        return false;
    }

    if (!ConfigureSerial(SerialPortStutas)) {
        close(SerialPortStutas);
        SerialPortStutas = -1;
        return false;
    }

    cout << "Serial port initialized successfully" << endl;
    return true;
}
bool Init::ExportGPIOX() {
    cout << "正在自动导出GPIO33..." << endl;

    // 1. 先检查GPIO33是否已导出（避免重复导出报错）
    DIR* dir = opendir(Gpio_Dir_Path.c_str());
    if (dir != NULL) {
        closedir(dir);
        cout << "GPIO33已导出，无需重复操作" << endl;
        return true;
    }

    // 2. 执行导出操作（需要root权限，因此程序必须用sudo运行）
    ofstream export_file(Gpio_Export_Path);
    if (!export_file.is_open()) {
        cerr << "GPIO导出文件打开失败！请确保用sudo运行程序" << endl;
        return false;
    }

    export_file << "33";  // 写入GPIO编号33
    export_file.close();

    // 3. 延迟100ms，确保系统完成导出
    usleep(100000);

    // 4. 验证导出是否成功
    dir = opendir(Gpio_Dir_Path.c_str());
    if (dir == NULL) {
        cerr << "GPIO33导出失败！请检查GPIO编号是否正确" << endl;
        return false;
    }

    closedir(dir);
    cout << "GPIO33导出成功！" << endl;
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
