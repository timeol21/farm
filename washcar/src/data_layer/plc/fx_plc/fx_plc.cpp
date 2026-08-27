#include "common/log/log_manager.h"
#include "data_layer/plc/fx_plc/fx_plc.h"

#include <iostream>

FxPlc::FxPlc(
    const std::string& port,
    int baudRate,
    const std::string& parity,
    int dataBits,
    int stopBits
)
:
port_(port),
baudRate_(baudRate),
parity_(parity),
dataBits_(dataBits),
stopBits_(stopBits)
{


}

FxPlc::~FxPlc()
{

    stop();

}

bool FxPlc::initialize()
{

    Logger::info(
        "[PLC] initialize"
    );


    if(port_.empty())
    {

        Logger::error(
            "[PLC] port empty"
        );


        return false;

    }



    if(baudRate_ <= 0)
    {

        Logger::error(
            "[PLC] invalid baudRate"
        );


        return false;

    }



    return true;

}

bool FxPlc::start()
{


    if(connected_)
    {
        return true;
    }

    Logger::info(
        "[PLC] starting"
    );

    Logger::info(
        "[PLC] port:" + port_
    );


    Logger::info(
        "[PLC] baudRate:" 
        + std::to_string(baudRate_)
    );

    /*
    
        这里以后是真实PLC通信


        例如:

        open("/dev/ttyS4")

        设置:

        baudRate
        parity
        dataBits
        stopBits


        初始化MC协议


    */



    connected_ = true;



    Logger::info(
        "[PLC] start successful"
    );



    return true;

}

void FxPlc::stop()
{

    if(!connected_)
    {
        return;
    }



    Logger::info(
        "[PLC] stopping"
    );



    /*
    
        以后:

        close serial port

        release resource


    */



    connected_=false;



    Logger::info(
        "[PLC] stopped"
    );

}


bool FxPlc::write(
    int address,
    int value
)
{


    if(!connected_)
    {

        Logger::error(
            "[PLC] not connected"
        );

        return false;

    }



    /*
    
        TODO:

        MC协议写入


        address:
            D100


        value:
            1


    */


    return true;

}


int FxPlc::read(
    int address
)
{


    if(!connected_)
    {

        return -1;

    }



    /*
    
        TODO:

        PLC读取


    */


    return 0;

}

