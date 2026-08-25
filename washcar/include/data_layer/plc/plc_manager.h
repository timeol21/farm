#pragma once
#include <memory>

class FxPlc;

class PlcManager
{

public:

    PlcManager();

    ~PlcManager();

    bool initialize();

    bool startMotor();

    bool stopMotor();

    bool readStatus();


private:

    std::unique_ptr<FxPlc> plc_;

};