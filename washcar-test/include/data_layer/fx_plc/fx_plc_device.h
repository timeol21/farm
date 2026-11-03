#pragma once
#include <string>

// FX 串口的参数容器。纯数据，不含逻辑。
//
// timeoutMs / frameDelayMs 来自 config.json 的 plcSerial 段：
//   timeoutMs    —— 单帧读超时，串口层转成 VTIME（单位 100ms，至少 1）
//   frameDelayMs —— 发完一帧后的静默间隔，RTU 要求 >= 3.5 字符时间
// 两者都带默认值，老的调用点不用改。
class FxPlcDevice {
public:
    FxPlcDevice(const std::string& plcId,
                const std::string& serialPort,
                int baudRate,
                int dataBits,
                bool parityEven,
                int stopBits,
                int timeoutMs = 200,
                int frameDelayMs = 10);

    const std::string& getPlcId() const;
    const std::string& getSerialPort() const;
    int getBaudRate() const;
    int getDataBits() const;
    bool getParityEven() const;
    int getStopBits() const;

    /// 单帧读超时（毫秒）。已做下限保护：至少 50ms。
    int getTimeoutMs() const;

    /// 帧间静默（毫秒）。已做下限保护：RTU 要求 >= 3.5 字符时间，至少 4ms。
    int getFrameDelayMs() const;

private:
    std::string plcId_;
    std::string serialPort_;
    int baudRate_;
    int dataBits_;
    bool parityEven_;
    int stopBits_;
    int timeoutMs_;
    int frameDelayMs_;
};
