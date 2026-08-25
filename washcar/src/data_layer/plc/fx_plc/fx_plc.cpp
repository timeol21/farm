#include "data_layer/plc/fx_plc/fx_plc.h"

#include <iostream>


bool FxPlc::connect(const std::string& ip)
{
    if (connected_)
    {
        return true;
    }


    /*
        TODO:
        这里添加真实PLC通信初始化

        例如：
        1. 创建TCP socket
        2. 设置PLC IP
        3. 建立MC协议连接
        4. 初始化通信参数


        当前先模拟连接成功
    */


    if (ip.empty())
    {
        return false;
    }


    connected_ = true;


    return true;
}



void FxPlc::disconnect()
{
    if (!connected_)
    {
        return;
    }


    /*
        TODO:
        这里关闭PLC连接

        例如：
        close(socket);

    */


    connected_ = false;
}



bool FxPlc::write(int address, int value)
{
    if (!connected_)
    {
        return false;
    }


    /*
        TODO:
        PLC写入

        address:
            PLC寄存器地址

        value:
            写入值


        例如:
        D100 = 1

    */


    return true;
}



int FxPlc::read(int address)
{
    if (!connected_)
    {
        return -1;
    }


    /*
        TODO:
        PLC读取


        当前模拟返回

    */


    return 0;
}

bool FxPlc::isConnected() const
{
    return connected_;
}