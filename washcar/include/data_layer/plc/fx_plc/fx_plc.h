#pragma once

#include "data_layer/plc/plc.h"

#include <string>


class FxPlc : public Plc
{

public:


    FxPlc(
        const std::string& port,
        int baudRate,
        const std::string& parity,
        int dataBits,
        int stopBits
    );


    ~FxPlc() override;


    bool initialize() override;


    bool start() override;


    void stop() override;



    bool write(
        int address,
        int value
    );


    int read(
        int address
    );


private:


    std::string port_;

    int baudRate_;

    std::string parity_;

    int dataBits_;

    int stopBits_;


    bool connected_ = false;


};