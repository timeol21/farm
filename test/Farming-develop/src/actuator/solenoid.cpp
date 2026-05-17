#include "actuator/solenoid.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/select.h>
#include <stdio.h>
#include <locale.h>

Solenoid::Solenoid(
    DeviceConfig cfg,
    DeviceState baseState
) : cfg_(cfg), baseState_(baseState) {}

bool Solenoid::init() {
    return initSerial(cfg_.channelConfig.port.c_str());
}

bool Solenoid::update(){
    return QuerySolenoidValveStatus();
}

void Solenoid::stop() {
    CloseSerial();
}

bool Solenoid::execute(const nlohmann::json& params) {
    std::string cmd = params.value("command", "");
    if(cmd == "open") {
        return OpenSolenoidValve();
    }else if(cmd == "close") {
        return CloseSolenoidValve();
    }
    return false;
}

bool Solenoid::ConfigureSerial(int fd) {
    // 当串口成功打开的时候，这个fd的值应该为3。
    /*
    fd = 3 → 代表这个串口的编号是 3
    Linux 不叫 COM1、COM2，它用数字代表每个打开的设备：
    fd = 0 → 键盘
    fd = 1 → 屏幕输出
    fd = 2 → 错误输出
    fd = 3 → 你打开的 /dev/ttyS4 串口
    */
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("串口配置失败（tcgetattr）");
        return false;
    }

    cfsetospeed(&tty, B19200);
    cfsetispeed(&tty, B19200);

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
        perror("串口参数设置失败（tcsetattr）");
        return false;
    }
    return true;
}

bool Solenoid::initSerial(const char *portName) {
    if(portName == nullptr || strlen(portName) == 0){
        std::cout<<"portName is null"<<std::endl;
        return false;
    }
    if (fd_ >= 0) {
        std::cout << "串口已处于初始化状态" << std::endl;
        return true;
    }
    
    std::cout<<portName<<std::endl;

    fd_ = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    // 只要打开成功，fd_ 就等于 3
    /*
    因为 Linux 系统有固定规则：程序启动时，系统自动占用 3 个文件描述符：
    0 = 标准输入（键盘）
    1 = 标准输出（屏幕）
    2 = 标准错误（报错信息）
    这三个是系统天生就占用的，你没打开任何文件，它们也存在。
    所以：你自己打开的第一个文件 / 串口 → 系统必须从 3 开始分配
    */
    if (fd_ < 0) {
        perror("串口打开失败");
        return false;
    }

    std::cout<<" ---------------------1--------------------"<<std::endl;
    if (!ConfigureSerial(fd_)) {
        std::cout<<"wrong"<<std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    std::cout << "串口初始化成功（设备：" << portName << "）" << std::endl;
    return true;
}

void Solenoid::CloseSerial() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        std::cout << "串口已关闭" << std::endl;
    }
}

// -------------------------- 电磁阀（Y1）控制函数 --------------------------
bool Solenoid::OpenSolenoidValve() {
    if (fd_ < 0) {
        std::cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << std::endl;
        return false;
    }

    // 电磁阀（Y1）开启指令（地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0xFF, 0x00, 0xDD, 0x36};
    ssize_t sent = write(fd_, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀开启指令发送失败");
        return false;
    }

    std::cout << "电磁阀开启指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;
    }

    std::cout << "电磁阀状态：已开启" << std::endl;
    return true;
}

bool Solenoid::CloseSolenoidValve() {
    if (fd_ < 0) {
        std::cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << std::endl;
        return false;
    }

    // 电磁阀（Y1）关闭指令（地址0x0501，校验码重新计算）
    unsigned char sendBuf[] = {0x01, 0x05, 0x05, 0x01, 0x00, 0x00, 0x9C, 0xC6};
    ssize_t sent = write(fd_, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀关闭指令发送失败");
        return false;
    }

    std::cout << "电磁阀关闭指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;
    }

    std::cout << "电磁阀状态：已关闭" << std::endl;
    return true;
}

// 电磁阀（Y1）状态查询
bool Solenoid::QuerySolenoidValveStatus() {
    if (fd_ < 0) {
        std::cerr << "错误：串口未初始化，请先返回主菜单初始化串口" << std::endl;
        return false;
    }

    // 电磁阀（Y1）查询指令（功能码0x01，查询Y1线圈状态，地址0x0501）
    unsigned char sendBuf[] = {0x01, 0x01, 0x05, 0x01, 0x00, 0x01, 0xAC, 0xC6};
    ssize_t sent = write(fd_, sendBuf, sizeof(sendBuf));

    if (sent != (ssize_t)sizeof(sendBuf)) {
        perror("电磁阀状态查询指令发送失败");
        return false;
    }

    std::cout << "电磁阀状态查询指令已发送：";
    for (size_t i = 0; i < sizeof(sendBuf); ++i)
        printf("%02X ", sendBuf[i]);
    std::cout << std::endl;

    unsigned char recvBuf[256];
    ssize_t len = read(fd_, recvBuf, sizeof(recvBuf));
    if (len > 0) {
        std::cout << "接收响应（" << len << "字节）：";
        for (ssize_t i = 0; i < len; ++i)
            printf("%02X ", recvBuf[i]);
        std::cout << std::endl;

        // 解析响应：第3字节为数据长度，第4字节bit0表示状态（1=开启，0=关闭）
        if (len >= 4) {
            if (recvBuf[3] & 0x01) {
                std::cout << "电磁阀当前状态：开启" << std::endl;
            } else {
                std::cout << "电磁阀当前状态：关闭" << std::endl;
            }
        } else {
            std::cout << "电磁阀状态查询响应无效" << std::endl;
        }
    } else {
        std::cout << "未接收到电磁阀状态响应" << std::endl;
    }

    return true;
}