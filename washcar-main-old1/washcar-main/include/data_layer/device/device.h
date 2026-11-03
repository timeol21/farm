#ifndef DEVICE_H
#define DEVICE_H

#include <string>
#include <any>
#include <memory>

#include "data_layer/device/device_status.h"
class Device {
    protected:
        
        Device(int type,std::string deviceId,std::string name)
            : type_(type), deviceId_(deviceId),name_(name) {};
    public:
        virtual ~Device() = default;

        virtual std::unique_ptr<DeviceStatus> getStatus() const = 0;

        Device() : type_(0) {};

        int getType() const { return type_;}

        std::string getDeviceId() const { return deviceId_;}

        std::string getName() const { return name_;}
    private:

        //设备类型
        int type_;

        std::string deviceId_;

        std::string name_;

};

#endif