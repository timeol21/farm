#include "business_layer/hall_service/hall_service.h"

#include "business_layer/hall_service/carwash_state.h"
#include "business_layer/hall_service/command_service.h"
#include "business_layer/hall_service/command_dispatcher.h"
#include "business_layer/hall_service/wash_service.h"
#include "business_layer/hall_service/security_service.h"
#include "business_layer/hall_service/ai_service.h"
#include "business_layer/hall_service/business_timer.h"

#include "data_layer/device_manager/device_manager.h"


HallService::HallService(LayerBufferQueue& bufferQueue)
:
bufferQueue_(bufferQueue)
{

}


HallService::~HallService()
{

    stop();

}


bool HallService::initialize()
{

    /*
        创建核心状态
    */

    carWashState_ = std::make_unique<CarWashState>();


    /*
        创建设备管理

        负责:

        PLC
        Camera
        Device

    */

    deviceManager_ = std::make_unique<DeviceManager>();


    /*
        创建业务服务

    */

    washService_ = std::make_unique<WashService>(*carWashState_,*deviceManager_); 


    commandService_ = std::make_unique<CommandService>(bufferQueue_);


    securityService_ = std::make_unique<SecurityService>();


    aiService_ = std::make_unique<AIService>();


    /*
        创建命令分发器

        依赖:

        CommandService

        WashService

        SecurityService

        AIService

    */

    commandDispatcher_ = std::make_unique<CommandDispatcher>(
            *commandService_,
            *washService_,
            *securityService_,
            *aiService_
        );


    /*
        创建业务定时器

    */

    timer_ = std::make_unique<BusinessTimer>();

    return true;

}


bool HallService::start()
{

    commandService_->start();

    commandDispatcher_->start();

    timer_->start(
        [this](){
            commandService_->processCommand();
            commandDispatcher_->dispatch();
        },100);


    return true;

}

void HallService::stop()
{

    if(timer_)
    {
        timer_->stop();
    }


    if(commandDispatcher_)
    {
        commandDispatcher_->stop();
    }


    if(commandService_)
    {
        commandService_->stop();
    }

}


CarWashState& HallService::carWashState()
{
    return *carWashState_;
}

DeviceManager& HallService::deviceManager()
{
    return *deviceManager_;
}

CommandService& HallService::commandService()
{
    return *commandService_;
}


CommandDispatcher& HallService::commandDispatcher()
{
    return *commandDispatcher_;
}


WashService& HallService::washService()
{
    return *washService_;
}


SecurityService& HallService::securityService()
{
    return *securityService_;
}


AIService& HallService::aiService()
{
    return *aiService_;
}


BusinessTimer& HallService::timer()
{
    return *timer_;
}