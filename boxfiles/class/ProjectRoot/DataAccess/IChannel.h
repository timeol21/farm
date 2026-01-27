#ifndef I_CHANNEL_H
#define I_CHANNEL_H

#include <string>

namespace BoxSystem {

class IChannel {
public:
    virtual ~IChannel() = default;
    virtual bool open(const std::string& info) = 0;
    virtual void close() = 0;
    virtual int send(const unsigned char* data, int len) = 0;
    virtual int receive(unsigned char* buffer, int maxLen) = 0;
    virtual std::string getPath() const = 0;
};

} // namespace BoxSystem

#endif