#include "data_layer/plc/plc_manager.h"
#include "common/log/log_manager.h"
#include <memory>


PlcManager::PlcManager()
{
}

PlcManager::~PlcManager()
{

    stop();

}

void PlcManager::addPlc(
    std::unique_ptr<Plc> plc
)
{

    if(!plc)
    {
        Logger::error(
            "[PLC] add plc failed"
        );

        return;
    }


    plcs_.push_back(
        std::move(plc)
    );

}


bool PlcManager::initialize()
{

    for(auto& plc : plcs_)
    {

        if(!plc)
        {
            continue;
        }


        if(!plc->initialize())
        {

            Logger::error(
                "[PLC] initialize failed"
            );

            return false;

        }

    }


    return true;

}



bool PlcManager::start()
{

    for(auto& plc : plcs_)
    {

        if(!plc)
        {
            continue;
        }


        if(!plc->start())
        {

            Logger::error(
                "[PLC] start failed"
            );

            return false;

        }

    }


    return true;

}

void PlcManager::stop()
{

    for(auto& plc : plcs_)
    {

        if(!plc)
        {
            continue;
        }


        plc->stop();

    }


}