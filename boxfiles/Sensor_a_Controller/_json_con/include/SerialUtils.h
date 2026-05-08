#ifndef SERIAL_UTILS_H
#define SERIAL_UTILS_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class SerialUtils {
private:
    int fd;                     // 串口文件描述符
    std::string portName;       // 串口名称

    // 配置串口参数
    bool configureSerial(const json& portConfig);

public:
    SerialUtils();
    ~SerialUtils();

    // 初始化串口
    bool initSerial(const std::string& portName, const json& portConfig);

    // 十六进制字符串转字节数组（兼容0x前缀）
    static std::vector<unsigned char> hexStringsToBytes(const std::vector<std::string>& hexStrings);

    // 发送数据
    bool sendData(const std::vector<unsigned char>& data);

    // 接收数据
    int recvData(unsigned char* buf, int bufSize, int waitMs = 100);

    // 关闭串口
    void closeSerial();

    // 获取串口文件描述符
    int getFd() const;

    // 检查串口是否初始化成功
    bool isInitialized() const;
};

#endif // SERIAL_UTILS_H