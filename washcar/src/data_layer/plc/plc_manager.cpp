#include "data_layer/plc/plc_manager.h"

#include "data_layer/plc/fx_plc/fx_plc.h"

#include <memory>


PlcManager::PlcManager()
{
}

PlcManager::~PlcManager() = default;

bool PlcManager::initialize()
{
    if (plc_)
    {
        return true;
    }


    plc_ = std::make_unique<FxPlc>();


    /*
        PLC连接参数

        后续应该来自:
        
        config.json

        例如:

        {
            "plc":
            {
                "type":"fx",
                "ip":"192.168.1.10"
            }
        }

    */


    if (!plc_->connect("192.168.1.10"))
    {
        plc_.reset();

        return false;
    }


    return true;
}



bool PlcManager::startMotor()
{
    if (!plc_)
    {
        return false;
    }


    /*
        这里不要直接操作PLC协议


        例如:

        M100 = 1

        这种属于 FxPlc


        PlcManager 只表达设备动作:

        启动电机

    */


    return plc_->write(
        100,
        1
    );
}



bool PlcManager::stopMotor()
{
    if (!plc_)
    {
        return false;
    }


    return plc_->write(
        100,
        0
    );
}



bool PlcManager::readStatus()
{
    if (!plc_)
    {
        return false;
    }


    /*
        假设:

        D100:
        0  停止
        1  运行
        2  故障

    */


    int status = plc_->read(100);


    if (status < 0)
    {
        return false;
    }


    return true;
}