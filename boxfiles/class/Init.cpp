#include <Init.h>
 
// 全局变量：串口文件描述符（供电磁阀函数使用）
int serialFd = -1;
// 传感器GPIO路径（可根据实际硬件修改）
const char* gpio_value_path = "/sys/class/gpio/gpio33/value";


Init::Init() {

}
#include "SerialManager.h"

// 构造函数实现：初始化成员变量
SerialManager::SerialManager(const std::string& port, const std::string& gpioPath) 
    : serialFd(-1),  // 串口文件描述符初始化为-1（未打开）
      gpioValuePath(gpioPath),
      portName(port) {
    // 构造函数无需额外逻辑，仅初始化成员
}

// 析构函数实现：自动关闭串口，防止资源泄漏
SerialManager::~SerialManager() {
    closeSerial(); // 调用关闭串口的方法
}

// 私有方法：串口参数配置（核心逻辑与原configureSerial一致，仅去掉fd参数（用成员变量serialFd））
bool SerialManager::configureSerial() {
    struct termios tty;
    // 读取当前串口属性
    if (tcgetattr(serialFd, &tty) != 0) {
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

    // 立即应用配置
    if (tcsetattr(serialFd, TCSANOW, &tty) != 0) {
        perror("串口参数设置失败（tcsetattr）");
        return false;
    }
    return true;
}

// 公有方法：编码初始化
void SerialManager::initEncoding() {
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL) {
        perror("设置编码失败");
    }
}

// 公有方法：串口初始化入口
bool SerialManager::initSerial() {
    // 检查串口是否已打开
    if (serialFd >= 0) {
        std::cout << "串口已处于初始化状态（端口：" << portName << "）" << std::endl;
        return true;
    }

    // 打开串口设备文件
    serialFd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (serialFd < 0) {
        perror(("串口打开失败（端口：" + portName + "）").c_str());
        return false;
    }

    // 调用私有方法配置串口参数
    if (!configureSerial()) {
        close(serialFd);
        serialFd = -1;
        return false;
    }

    std::cout << "串口初始化成功（设备：" << portName << "）" << std::endl;
    return true;
}

// 公有方法：关闭串口
void SerialManager::closeSerial() {
    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        std::cout << "串口已关闭（端口：" << portName << "）" << std::endl;
    }
}

// 公有接口：获取串口文件描述符
int SerialManager::getSerialFd() const {
    return serialFd;
}

// 公有接口：设置GPIO路径
void SerialManager::setGpioValuePath(const std::string& path) {
    gpioValuePath = path;
}

// 公有接口：获取GPIO路径
std::string SerialManager::getGpioValuePath() const {
    return gpioValuePath;
}

// 公有接口：设置串口端口名
void SerialManager::setPortName(const std::string& port) {
    // 若串口已打开，先关闭再修改端口名
    if (serialFd >= 0) {
        closeSerial();
    }
    portName = port;
}

// 公有接口：获取串口端口名
std::string SerialManager::getPortName() const {
    return portName;
}
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