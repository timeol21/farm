#pragma once


#include <string>
#include <vector>


#include "common/config/machine_config.h"



class BoxConfig
{

public:

    BoxConfig() = default;


    ~BoxConfig() = default;
    
    


public:


    // box id

    std::string id;



    // machines inside this box

    std::vector<MachineConfig> machines;


};