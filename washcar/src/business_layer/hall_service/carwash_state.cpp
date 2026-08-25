#include "business_layer/hall_service/carwash_state.h"



CarWashState::CarWashState()
:
currentState_(WashMachineState::READY)
{

}

WashMachineState CarWashState::state() const
{

    std::lock_guard<std::mutex> lock(
        mutex_
    );


    return currentState_;

}

void CarWashState::setState(WashMachineState state)
{

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    currentState_ = state;

}

bool CarWashState::canWash() const
{

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    switch(currentState_)
    {

        case WashMachineState::READY:
        {
            return true;
        }

        case WashMachineState::WASHING:
        {
            return false;
        }

        case WashMachineState::MAINTENANCE:
        {
            return false;
        }

        case WashMachineState::ERROR:
        {
            return false;
        }

        default:
        {
            return false;
        }

    }

}