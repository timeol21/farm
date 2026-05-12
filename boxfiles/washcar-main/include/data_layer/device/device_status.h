#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <string>
class DeviceStatus {

    protected:
        
        DeviceStatus(const std::string& deviceId,
                     const int type,
                     const std::string& name)
            : deviceId_(deviceId),
              type_(type),
              name_(name) {}

    public:
        virtual ~DeviceStatus() = default;

        DeviceStatus() : type_(0) {}
        
        std::string getDeviceId() const {return deviceId_;}

        int getType() {return type_;}
    
    private:
        std::string deviceId_;
        int type_;
        std::string name_;
};

#endif