#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include <string>
#include <termios.h>
class SerialConfig {

    public:
        SerialConfig() = default;
        SerialConfig(int baudRate,
                     int dataBits,
                     int stopBits,
                     std::string parity);
        ~SerialConfig() = default;

        speed_t getBaudRate() const;
        int getDataBits() const;
        void setPlcDataBits(struct termios& tty);
        int getStopBits() const;
        void setPlcStopBits(struct termios& tty);
        std::string getParity() const;
        void setPlcParity(struct termios& tty);

    private:
        int baudRate_;
        int dataBits_;
        int stopBits_;
        std::string parity_;
};

#endif