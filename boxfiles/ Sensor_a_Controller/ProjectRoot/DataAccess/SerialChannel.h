#ifndef SERIAL_CHANNEL_H
#define SERIAL_CHANNEL_H

#include "IChannel.h"
#include <string>

namespace BoxSystem {

class SerialChannel : public IChannel {
private:
    int fd = -1;                // 文件描述符
    std::string portPath;       // 串口路径（如 /dev/ttyS4）

public:
    SerialChannel() = default;
    ~SerialChannel() override;

    // 实现基类接口
    bool open(const std::string& info) override;
    void close() override;
    int send(const unsigned char* data, int len) override;
    int receive(unsigned char* buffer, int maxLen) override;
    std::string getPath() const override { return portPath; }
};

} // namespace BoxSystem

#endif