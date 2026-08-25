#ifndef IALARM_H
#define IALARM_H

#include <string>

class IAlarm {
public:
    virtual ~IAlarm() = default;
    virtual bool initialize() = 0;
    virtual void trigger() = 0;
    virtual void stop() = 0;
    virtual void setDuration(int seconds) = 0;
    virtual bool isTriggered() const = 0;
    virtual std::string getType() const = 0;
};

#endif