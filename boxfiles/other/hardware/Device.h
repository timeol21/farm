// #pragma once
#include <string>

class Device{
    public:
        Device(
            int id,
            const std::string& deviceId,
            const std::string& deviceState

        );

        int getId () const;
        std::string getDeviceId () const;
        std::string getDeviceState () const;

    private:
        int id_;
        std::string deviceId_;
        std::string deviceState_;
};

