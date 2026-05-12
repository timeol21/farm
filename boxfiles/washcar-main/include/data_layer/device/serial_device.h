#ifndef SERIAL_DEVICE_H
#define SERIAL_DEVICE_H

#include "data_layer/device/device.h"
class SerialDevice : public Device{
    protected:
        SerialDevice(int type,const std::string deviceId,const std::string name,const std::string bindSerialPort)
            : Device(type,deviceId,name),
              bindSerialPort_(bindSerialPort) {}

    public:
        virtual ~SerialDevice() = default;

        std::string getBindSerialPort() const { return bindSerialPort_;}
        // std::string getSlaveAddr() const { return slaveAddr_;}
        // std::string getRegAddr() const { return regAddr_;}

    private:
        
        //绑定串口
        std::string bindSerialPort_;
        // //从机地址
        // std::string slaveAddr_;
        // //寄存器地址
        // std::string regAddr_;
};

#endif