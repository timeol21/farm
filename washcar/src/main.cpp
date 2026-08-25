#include "common/log/log_manager.h"
#include "common/log/logger.h"

#include "data_layer/data_layer.h"
#include "business_layer/business_layer.h"
#include "userinterface_layer/user_interface.h"

int main()
{
    LogManager::instance().init("washcar.log");
    Logger::info("[System] starting");
    
    DataLayer dataLayer;

    if(!dataLayer.initialize())
    {
        Logger::error("[System] data layer initialize failed");

        return -1;
    }

    if(!BusinessLayer::instance().initialize())
    {
        Logger::error("[System] business layer initialize failed");

        return -1;
    }

    if(!UserInterface::instance().initialize())
    {
        Logger::error("[System] user interface initialize failed");

        return -1;
    }

    if(!UserInterface::instance().start())
    {
        Logger::error("[System] user interface start failed");

        return -1;
    }

    Logger::info("[System] running");

    return 0;
}

