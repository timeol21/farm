#include "business_layer/hall_service/command_dispatcher.h"

#include "business_layer/hall_service/command_service.h"
#include "business_layer/hall_service/wash_service.h"
#include "business_layer/hall_service/security_service.h"
#include "business_layer/hall_service/ai_service.h"


CommandDispatcher::CommandDispatcher(
    CommandService& commandService,
    WashService& washService,
    SecurityService& securityService,
    AIService& aiService
)
:
commandService_(commandService),
washService_(washService),
securityService_(securityService),
aiService_(aiService)
{

}


bool CommandDispatcher::start()
{

    if(running_)
    {
        return true;
    }


    running_ = true;


    return true;

}


void CommandDispatcher::stop()
{

    running_ = false;

}


void CommandDispatcher::dispatch()
{

    if(!running_)
    {
        return;
    }


    CommandMessage message;


    while(commandService_.popPendingCommand(message))
    {

        handleCommand(message);

    }

}


void CommandDispatcher::handleCommand(
    const CommandMessage& message
)
{

    switch(message.type)
    {

        case CommandType::START_WASH:
        {
            washService_.execute(message);

            break;
        }

        case CommandType::STOP_WASH:
        {
            washService_.execute(message);

            break;
        }

        case CommandType::PAUSE_WASH:
        {
            washService_.execute(message);

            break;
        }


        case CommandType::RESET_DEVICE:
        {
            // securityService_.execute(message);

            break;
        }


        case CommandType::QUERY_STATUS:
        {
            // aiService_.execute(message);

            break;
        }


        default:
        {
            break;
        }

    }

}