#ifndef I_COMAND_DISPATCHER_H
#define I_COMAND_DISPATCHER_H
#include <string>

class ICommandDispatcher {
public:
    virtual ~ICommandDispatcher() = default;
    virtual void onMessage(const std::string& topic,
                           const std::string& payload) = 0;
};

#endif