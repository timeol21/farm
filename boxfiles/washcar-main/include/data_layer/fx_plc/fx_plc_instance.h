#pragma once
#include "data_layer/fx_plc/fx_plc_device.h"
#include <cstdint>
#include <mutex>

class FxPlcInstance {
public:
    explicit FxPlcInstance(const FxPlcDevice& device);
    ~FxPlcInstance();

    bool connect();                 // 打开串口，配置参数
    void disconnect();              // 关闭串口
    bool isConnected() const;
    const std::string& getPlcId() const;
    // 控制 Y 点
    bool forceY(int yOctal, bool turnOn);
    // 控制 M 点
    bool forceM(int mDecimal, bool turnOn);
    // 读取 D 寄存器
    bool readD(int dNumber, uint16_t& value);
    // 读取单个 Y 点状态
    bool readYBit(int yOctal, bool& state);
    // 读取单个 M 点状态
    bool readM(int mDecimal, bool& state);
    //读取单个y点状态
    bool readXBit(int xOctal, bool& state);
    //读取一组x点
    bool readX(int group, int& status8);
    // 读取一组 Y 点（8个，按组号 0~9）
    bool readY(int group, int& status8);
    //const std::string& getPlcId() const { return device_.getPlcId(); }
    //读取s点
    bool readS(int sDecimal, bool& state);
private:
    FxPlcDevice device_;
    int serialFd_;                 
    bool connected_;
    std::mutex mutex_; 
    //x点地址计算 
    int xOctalToAddr(int xOctal); 
    // 底层串口读写（复用用户提供的函数）
    int openSerial(const char* dev, int baud, int dataBits, bool parityEven, int stopBits);
    bool sendAndReceive(const unsigned char* sendBuf, int sendLen,
                        unsigned char* recvBuf, int* recvLen);
    void calculateAndAppendSum(unsigned char* buf, int startIdx, int len);

    int yOctalToAddr(int yOctal);
    int mDecimalToForceAddr(int mDecimal);
    int mDecimalToReadAddr(int mDecimal);
};