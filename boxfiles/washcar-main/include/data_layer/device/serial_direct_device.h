#ifndef SERIAL_DIRECT_DEVICE_H
#define SERIAL_DIRECT_DEVICE_H

#include "data_layer/device/serial_device.h"
class SerialDirectDevice : public SerialDevice{
    
    protected:
        SerialDirectDevice(int type,
                           const std::string deviceId,
                           const std::string name,
                           const std::string bindSerialPort,
                           const std::string slaveAddr,
                           const std::string regAddr )
            : SerialDevice(type,deviceId,name,bindSerialPort),
              slaveAddr_(slaveAddr),
              regAddr_(regAddr) {}
    public:
        virtual ~SerialDirectDevice() = default;

        std::string getSlaveAddr() const { return slaveAddr_;}
        std::string getRegAddr() const {return regAddr_;}

    private:
        //从机地址
        std::string slaveAddr_;
        //寄存器地址
        std::string regAddr_;
};

#endif