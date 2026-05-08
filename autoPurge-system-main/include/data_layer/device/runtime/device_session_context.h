#pragma once

#include <string>
#include <memory>



class IDeviceSessionContext{
public:

    virtual ~IDeviceSessionContext();

    virtual std::string sessionType() const = 0;

    virtual bool isValid() const = 0;

};

//海康设备（nvr）会话上下文

class HikvisionDeviceSessionContext : public IDeviceSessionContext{
public:

    HikvisionDeviceSessionContext(long userId,const std::string& deviceId,const std::string& ip);

    ~HikvisionDeviceSessionContext() override;

    std::string sessionType() const override;

    bool isValid() const override;

private:

    long userId_ = -1;
    std::string deviceId_;
    std::string ip_;
    
};