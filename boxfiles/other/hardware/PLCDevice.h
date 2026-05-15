// #pragma once
#include<string>

#include "Device.h"

class PLCDevice : public Device {
    public:
        PLCDevice(
            int id,
            const std::string& deviceId,
            const std::string& deviceState,
            const std::string& portName,
            int portState,
            int plcId
        );
    protected:
        int getPortState() const { return portState_; };

    private:
        bool InitSerial(const std::string& portName);
        bool ConfigSerial(int fd);
        bool CloseSerial();

        std::string portName_;
        int plcId_;

        int portState_;

};