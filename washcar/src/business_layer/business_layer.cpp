#include "business_layer/business_layer.h"

#include "business_layer/hall_service/hall_service.h"



BusinessLayer& BusinessLayer::instance()
{
    static BusinessLayer instance;

    return instance;
}



bool BusinessLayer::initialize()
{

    hallService_ = std::make_unique<HallService>();

    if(!hallService_->initialize())
    {
        return false;
    }

    return true;
}

HallService& BusinessLayer::hallService()
{
    return *hallService_;
}