#pragma once
#include <string>

class FxPlcDevice {
public:
    FxPlcDevice(const std::string& plcId,
                const std::string& serialPort,
                int baudRate,
                int dataBits,
                bool parityEven,
                int stopBits);
    const std::string& getPlcId() const;
    const std::string& getSerialPort() const;
    int getBaudRate() const;
    int getDataBits() const;
    bool getParityEven() const;
    int getStopBits() const;
private:
    std::string plcId_;
    std::string serialPort_;
    int baudRate_;
    int dataBits_;
    bool parityEven_;
    int stopBits_;
};