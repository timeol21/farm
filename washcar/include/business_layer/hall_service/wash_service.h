#pragma once

#include "common/message/command_message.h"


class CarWashState;
class DeviceManager;


class WashService
{

public:

    WashService(
        CarWashState& state,
        DeviceManager& deviceManager
    );


    bool initialize();


    bool execute(
        const CommandMessage& command
    );


private:

    bool startWash();

    bool stopWash();


private:

    CarWashState& state_;

    DeviceManager& deviceManager_;

};