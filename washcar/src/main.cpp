#include "common/log/log_manager.h"
#include "common/log/logger.h"
#include "common/layer_buffer_queue/layer_buffer_queue.h"
#include "data_layer/data_layer.h"
#include "business_layer/business_layer.h"
#include "userinterface_layer/user_interface.h"

int main()
{
    LogManager::instance().init("/home/ztl/program/washcar/washcar.log");
    Logger::info("[System] starting");
    
    LayerBufferQueue layerBufferQueue;
    
    DataLayer dataLayer;

    if(!dataLayer.initialize())
    {
        Logger::error("[System] data layer initialize failed");

        return -1;
    }else{
        Logger::info("[System] data layer initialize successful");
    }

    BusinessLayer businessLayer(layerBufferQueue);

    if(!businessLayer.initialize())
    {
        Logger::error("[System] business layer initialize failed");
    
        return -1;
    }else{
        Logger::info("[System] business layer initialize successful");
    }

    UserInterface userInterface(layerBufferQueue);

    if(!userInterface.initialize())
    {
        Logger::error("[System] user interface initialize failed");
    
        return -1;
    }else{
        Logger::info("[System] user interface initialize successful");
    }

    if(!userInterface.start())
    {
        Logger::error("[System] user interface start failed");
    
        return -1;
    }else{
        Logger::info("[System] user interface start successful");
    }

    Logger::info("[System] running");

    return 0;
}

