#pragma once

#include <mutex>

enum class WashMachineState
{
    READY,

    WASHING,

    MAINTENANCE,

    ERROR

};



class CarWashState
{

public:

    CarWashState();

    WashMachineState state() const;

    void setState(WashMachineState state);

    bool canWash() const;

private:

    WashMachineState currentState_;

    mutable std::mutex mutex_;

};