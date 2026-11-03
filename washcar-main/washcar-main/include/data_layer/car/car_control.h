#ifndef CAR_CONTROL_H
#define CAR_CONTROL_H

#include <string>

class CarControl {
    public:

        CarControl() = default;
        CarControl(const std::string& deviceId) ;

        const std::string& getDeviceId() const ;
    private:
        std::string deviceId_;
};

#endif