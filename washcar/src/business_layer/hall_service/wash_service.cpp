#include "business_layer/hall_service/wash_service.h"

#include "business_layer/hall_service/carwash_state.h"

#include "data_layer/device_manager/device_manager.h"

#include <iostream>


WashService::WashService(
    CarWashState& state,
    DeviceManager& deviceManager
)
:
state_(state),
deviceManager_(deviceManager)
{

}


bool WashService::initialize()
{

    return true;

}


bool WashService::execute(
    const CommandMessage& command
)
{

    switch(command.type)
    {

        case CommandType::START_WASH:
        {
            return startWash();
        }


        case CommandType::STOP_WASH:
        {
            return stopWash();
        }


        default:
        {
            return false;
        }

    }

}


bool WashService::startWash()
{

    if(!state_.canWash())
    {
        return false;
    }


    state_.setState(WashMachineState::WASHING);


    /*
        调用设备层

        DeviceManager

        控制PLC开始洗车

    */

    std::cout << "WashService start wash" << std::endl;

    return true;

}


bool WashService::stopWash()
{
    /*
        通知设备停止

        DeviceManager

    */
    state_.setState(WashMachineState::READY);

    std::cout << "WashService stop wash" << std::endl;

    return true;

}