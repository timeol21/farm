#pragma once
#include<string>
#include <unordered_map>      
#include <memory> 
           
#include "Device.h"

class Solenoid;

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
        void addSolenoid(std::shared_ptr<Solenoid> solenoid);
        std::shared_ptr<Solenoid> getSolenoid(int solenoidId);

    protected:
        int getPortState() const { return portState_; };
        int getPlcId() const { return plcId_; };
        std::string getPlcPortName() const { return portName_; };
        bool plcInitSerial();
        bool plcCloseSerial();
       
    private:
        bool initSerial(const std::string& portName);
        bool configSerial(int fd);
        bool closeSerial();

        std::unordered_map<int, std::shared_ptr<Solenoid>> solenoidSet_;

        std::string portName_;
        int plcId_;
        int portState_;

};