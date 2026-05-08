#include "GpioUtils.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <limits>

GpioUtils::GpioUtils() : pin(-1) {}

GpioUtils::~GpioUtils() {
    // 程序退出时自动释放GPIO
    if (pin > 0) {
        unexportGPIO();
    }
}

// 修正：成员函数，正确实现
std::string GpioUtils::getGpioSysPath() const {
    return "/sys/class/gpio/gpio" + std::to_string(pin);
}

// 修正：函数名改为getGpioValuePath，正确实现
std::string GpioUtils::getGpioValuePath() const {
    return getGpioSysPath() + "/value";
}

bool GpioUtils::gpioExists() const {
    struct stat st;
    return stat(getGpioSysPath().c_str(), &st) == 0;
}

bool GpioUtils::initGPIO(const json& config) {
    gpioConfig = config;
    pin = config["pin"].get<int>();

    // 检查root权限
    if (!checkRoot()) {
        std::cerr << "错误：必须使用root权限运行（请加sudo）" << std::endl;
        return false;
    }

    // 导出GPIO
    if (!gpioExists()) {
        std::cout << "正在导出GPIO" << pin << "..." << std::endl;
        if (!exportGPIO()) {
            return false;
        }
        usleep(200000); // 等待sysfs节点初始化
    } else {
        std::cout << "GPIO" << pin << "已导出，直接使用" << std::endl;
    }

    // 设置方向
    std::string dir = config["direction"].get<std::string>();
    if (!setDirection(dir)) {
        return false;
    }

    return true;
}

// 修正：静态函数移除const限定符
bool GpioUtils::checkRoot() {
    return geteuid() == 0;
}

bool GpioUtils::exportGPIO() {
    std::ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open()) {
        std::cerr << "错误：无法打开export文件（" << strerror(errno) << "）" << std::endl;
        return false;
    }
    exportFile << pin;
    exportFile.close();
    return true;
}

bool GpioUtils::unexportGPIO() {
    std::ofstream unexportFile("/sys/class/gpio/unexport");
    if (!unexportFile.is_open()) {
        std::cerr << "错误：无法释放GPIO（" << strerror(errno) << "）" << std::endl;
        return false;
    }
    unexportFile << pin;
    unexportFile.close();
    std::cout << "GPIO" << pin << "已释放" << std::endl;
    return true;
}

bool GpioUtils::setDirection(const std::string& dir) {
    if (dir != "in" && dir != "out") {
        std::cerr << "错误：方向参数必须是in或out" << std::endl;
        return false;
    }

    std::string dirPath = getGpioSysPath() + "/direction";
    std::ofstream dirFile(dirPath);
    if (!dirFile.is_open()) {
        std::cerr << "错误：无法设置GPIO方向（" << strerror(errno) << "）" << std::endl;
        return false;
    }
    dirFile << dir;
    dirFile.close();
    std::cout << "GPIO" << pin << "已设置为" << dir << "模式" << std::endl;
    return true;
}

bool GpioUtils::setValue(int value) {
    if (value != 0 && value != 1) {
        std::cerr << "错误：电平值必须是0或1" << std::endl;
        return false;
    }

    // 修正：调用正确的函数名getGpioValuePath()
    std::ofstream valueFile(getGpioValuePath());
    if (!valueFile.is_open()) {
        std::cerr << "错误：无法设置GPIO电平（" << strerror(errno) << "）" << std::endl;
        return false;
    }
    valueFile << value;
    valueFile.close();
    
    std::string state = (value == 1) ? "高电平（3.3V）" : "低电平（0V）";
    std::cout << "GPIO" << pin << "已设置为" << state << std::endl;
    return true;
}

int GpioUtils::getValue() const {
    // 修正：调用正确的函数名getGpioValuePath()
    std::ifstream valueFile(getGpioValuePath());
    if (!valueFile.is_open()) {
        std::cerr << "错误：无法读取GPIO电平（" << strerror(errno) << "）" << std::endl;
        return -1;
    }
    std::string valueStr;
    valueFile >> valueStr;
    valueFile.close();
    return std::stoi(valueStr);
}

void GpioUtils::printGpioInfo() const {
    std::cout << "---------------- GPIO设备信息 ----------------" << std::endl;
    std::cout << "设备名称：" << gpioConfig["name"] << std::endl;
    std::cout << "GPIO组号：" << gpioConfig["gpio_group"] << std::endl;
    std::cout << "组内引脚：" << gpioConfig["gpio_pin_num"] << std::endl;
    std::cout << "物理编号：" << pin << std::endl;
    std::cout << "电压域：" << gpioConfig["voltage_domain"] << std::endl;
    std::cout << "有效电平：" << gpioConfig["active_logic"] << std::endl;
    std::cout << "当前电平：" << (getValue() == 1 ? "高电平（3.3V）" : "低电平（0V）") << std::endl;
    std::cout << "描述：" << gpioConfig["description"] << std::endl;
    std::cout << "----------------------------------------------" << std::endl;
}