#ifndef ICHANNEL_H
#define ICHANNEL_H

namespace BoxSystem {
class IChannel {
public:
    virtual ~IChannel() = default;
    virtual bool open() = 0;
    virtual void close() = 0;
    // 统一签名：int 返回值，包含 const 约束
    virtual int send(const unsigned char* data, int len) = 0;
    virtual int receive(unsigned char* buffer, int maxLen) = 0;
};
}
#endif