#pragma once

#include <vector>
#include <memory>

#include "data_layer/plc/plc.h"


class PlcManager
{

public:

    PlcManager();

    ~PlcManager();


    bool initialize();

    bool start();

    void stop();



    void addPlc(
        std::unique_ptr<Plc> plc
    );


private:

    std::vector<
        std::unique_ptr<Plc>
    > plcs_;


};