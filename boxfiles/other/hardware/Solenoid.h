// #pragma once
#include<string>
#include<vector>
#include "PLCDevice.h"

class Solenoid : public PLCDevice{
    public:
        Solenoid(
            int id,
            const std::string& deviceId,
            const std::string& deviceState,
            const std::string& portName,
            int plcId,
            int portState,
            const std::string& solenoidName,
            int solenoidId

        );

    private:
        std::string solenoidName_;
        int solenoidId_;
        bool OpenSolenoid(const std::vector<uint8_t>& sendCmd);
        bool CloseSolenoid(const std::vector<uint8_t>& sendcmd);
        bool QuerySolenoid(const std::vector<uint8_t>& sendcmd);

};