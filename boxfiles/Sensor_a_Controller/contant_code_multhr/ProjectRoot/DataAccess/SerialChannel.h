#ifndef SERIAL_CHANNEL_H
#define SERIAL_CHANNEL_H

#include "IChannel.h"
#include <string>
#include <mutex> // 必须引入
#include <unistd.h>

namespace BoxSystem {
class SerialChannel : public IChannel {
private:
    int fd;
    std::string portName;
    std::mutex mtx; // 串口资源锁

public:
    SerialChannel(const std::string& port, int baudRate);
    ~SerialChannel();

    bool open() override;
    void close() override;
    
    // 使用 lock_guard 确保线程安全
    int send(const unsigned char* data, int len) override;
    int receive(unsigned char* buffer, int maxLen) override;
};
}
#endif