#ifndef GPIO_UTILS_H
#define GPIO_UTILS_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class GpioUtils {
private:
    json gpioConfig;            // GPIO配置
    int pin;                    // GPIO物理编号

    // 获取GPIO sysfs路径（成员函数，可加const）
    std::string getGpioSysPath() const;
    // 获取GPIO value路径（修正函数名）
    std::string getGpioValuePath() const;

    // 检查GPIO是否已导出
    bool gpioExists() const;

public:
    GpioUtils();
    ~GpioUtils();

    // 初始化GPIO配置
    bool initGPIO(const json& config);

    // 检查是否为root权限（静态函数，无const）
    static bool checkRoot();

    // 导出GPIO
    bool exportGPIO();

    // 释放GPIO
    bool unexportGPIO();

    // 设置GPIO方向（in/out）
    bool setDirection(const std::string& dir);

    // 设置GPIO电平（0/1）
    bool setValue(int value);

    // 获取当前GPIO电平
    int getValue() const;

    // 打印GPIO信息
    void printGpioInfo() const;
};

#endif // GPIO_UTILS_H