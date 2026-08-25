#pragma once
#include <string>

class FxPlc
{

public:
    bool connect(const std::string& ip);

    void disconnect();

    bool write(int address,int value);

    int read(int address);
    
    bool isConnected() const;

private:

    bool connected_ = false;

};