#pragma once

//获取为unclog服务的设备信息
#include "common/config/config_load.h"
#include "data_layer/unclog/unclog_data_layer_object.h"
class IUnclogDao
{
public:
    
    virtual ~IUnclogDao() = default;

    // 获取监测时间
    virtual bool getMonitorTime() = 0; 

    // 获取设备信息
    virtual RawDeviceInfo getDeviceInfo() = 0;


    virtual bool getUnclogRecords() = 0;
    
};





class UnclogDao : public IUnclogDao{
public:
    UnclogDao();

    ~UnclogDao();

    bool getMonitorTime();

        
    RawDeviceInfo getDeviceInfo();


    bool getUnclogRecords();


private:
    





};