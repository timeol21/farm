#include "data_layer/plc/plc_factory.h"

#include "data_layer/plc/fx_plc/fx_plc.h"

#include "common/log/log_manager.h"



PlcFactory::PlcFactory()
{

}



PlcFactory::~PlcFactory()
{

}



std::unique_ptr<Plc>
PlcFactory::create(
    const DeviceConfig& config
)
{

    if(config.type != "plc")
    {

        Logger::error(
            "[PLC Factory] invalid device type"
        );

        return nullptr;

    }



    if(config.model == "fx3u")
    {

        const auto& params =
            config.parameters;



        if(
            params.find("port")
            ==
            params.end()
            ||
            params.find("baudRate")
            ==
            params.end()
            ||
            params.find("parity")
            ==
            params.end()
            ||
            params.find("dataBits")
            ==
            params.end()
            ||
            params.find("stopBits")
            ==
            params.end()
        )
        {

            Logger::error(
                "[PLC Factory] missing parameter"
            );


            return nullptr;

        }



        return std::make_unique<FxPlc>(

            params.at("port").get<std::string>(),


            params.at("baudRate").get<int>(),
        
        
            params.at("parity").get<std::string>(),
        
        
            params.at("dataBits").get<int>(),
        
        
            params.at("stopBits").get<int>()

        );

    }



    Logger::error(
        "[PLC Factory] unsupported model"
    );


    return nullptr;

}