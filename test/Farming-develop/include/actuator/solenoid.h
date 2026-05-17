#pragma once
#include "i_actuator.h"
#include "config/config.h"
#include "utils/json.hpp"

#include<string>

class Solenoid : public IActuator {
    public:
        Solenoid(DeviceConfig cfg, DeviceState baseState);
        bool init() override;
        bool update() override;
        void stop() override;
        const DeviceState& getState() const override { return baseState_; };
        std::string getDeviceId() const override { return cfg_.id; };
        bool execute(const nlohmann::json& params) override;

    private:
        bool initSerial(const char *portName);
        bool ConfigureSerial(int fd);
        bool OpenSolenoidValve();
        bool CloseSolenoidValve();
        bool QuerySolenoidValveStatus();
        void CloseSerial();

        int fd_ = -1;
        DeviceConfig cfg_;
        DeviceState baseState_;


};

