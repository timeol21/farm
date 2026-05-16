#pragma once
#include<string>
#include<vector>
#include <memory> 
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
        int getSolenoidId() {return solenoidId_;};
        std::string getSolenoidName() {return solenoidName_;};
        void openCurrentSolenoid();
        void queryCurrentSolenoid();
        void closeCurrentSolenoid();
        
        
    private:
        std::string solenoidName_;
        int solenoidId_;
        bool openSolenoid(const std::vector<uint8_t>& sendCmd);
        bool closeSolenoid(const std::vector<uint8_t>& sendcmd);
        bool querySolenoid(const std::vector<uint8_t>& sendcmd);

};