#ifndef TEMP_HUMID_SENSOR_H
#define TEMP_HUMID_SENSOR_H

#include <string>
#include "data_layer/sensor/temp_humid_sensor_status.h"
#include "data_layer/device/serial_direct_device.h"
#include "data_layer/serial/serial_config.h"
class TempHumidSensor : public SerialDirectDevice {  
    public:
        TempHumidSensor(int type,
               const std::string& deviceId,
               const std::string& name,
               const std::string& bindSerialPort,
               const std::string& slaveAddr,
               const std::string& regAddr,
               const int readRegs,
               SerialConfig serialConfig);
        ~TempHumidSensor();

        std::unique_ptr<DeviceStatus> getStatus() const ;

        TempHumidSensorStatus readSensorData();
        //打开串口
        bool connect();
        void disconnect();
        // SensorStatus getStatus();
        // SensorRealTimeData getRealTimeData();

    private:
        bool configureSerial();

        std::array<uint8_t,8> buildReadDataCommand();
        bool sendData(const std::array<uint8_t,8>& data);
        int recviceData(unsigned char* recvBuf, int bufSize, int waitTime);

        void splitRegAddress(uint8_t& high,uint8_t& low);
        uint16_t buildCalcCRC(const uint8_t* data,size_t length);
        
        SerialConfig serialConfig_;
        int readRegs_;
        int serialPortStatus_ = -1;

};

#endif