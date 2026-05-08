#pragma once 

class ILifecycle {
public:
    virtual void start() = 0;
    virtual void stop() = 0;
};