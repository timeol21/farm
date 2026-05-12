#include "data_layer/serial/serial_config.h"

SerialConfig::SerialConfig(int baudRate,
                           int dataBits,
                           int stopBits,
                           std::string parity)
    : baudRate_(baudRate),
      dataBits_(dataBits),
      stopBits_(stopBits),
      parity_(parity){

}

speed_t SerialConfig::getBaudRate() const{
  switch (baudRate_)
  {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B9600;
  }
}

void SerialConfig::setPlcDataBits(struct termios& tty ) {
  tty.c_cflag &= ~CSIZE;

  switch( dataBits_ ) {
      case 5:
          tty.c_cflag |= CS5;
          break;
      case 6:
          tty.c_cflag |= CS6;
          break;
      case 7:
          tty.c_cflag |= CS7;
          break;
      case 8:
      default:
          tty.c_cflag |= CS8;
          break;
  }
}

void SerialConfig::setPlcStopBits(struct termios& tty) {
  if( stopBits_ == 2)
    tty.c_cflag |= CSTOPB;
  else
    tty.c_cflag &= ~CSTOPB;
}

void SerialConfig::setPlcParity(struct termios& tty) {
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~PARODD;

  if( parity_ == "even") {
    tty.c_cflag |= PARENB;
  }
  else if(parity_ == "odd") {
    tty.c_cflag |= PARENB;
    tty.c_cflag |= PARODD;
  }
}

int SerialConfig::getDataBits() const{
  return dataBits_;
}

int SerialConfig::getStopBits() const{
  return stopBits_;
}

std::string SerialConfig::getParity() const{
  return parity_;
}