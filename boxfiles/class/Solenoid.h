#ifndef SOLENOID_H
#define SOLENOID_H

class Solenoid{
    private:

    public:
    bool OpenSolenoidValve();
    bool CloseSolenoidValve();
    void DetectSensorStatus();
};
#endif