#include <fcntl.h> 
#include <unistd.h>
#include <termios.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <array>
#include "data_layer/sensor/temp_humid_sensor.h"

TempHumidSensor::TempHumidSensor(int type,
                                       const std::string& deviceId,
                                       const std::string& name,
                                       const std::string& bindSerialPort,
                                       const std::string& slaveAddr,
                                       const std::string& regAddr,
                                       const int readRegs,
                                       SerialConfig serialConfig)
    : SerialDirectDevice(type,deviceId,name,bindSerialPort,slaveAddr,regAddr),
      readRegs_(readRegs),serialConfig_(serialConfig)
{

}

TempHumidSensor::~TempHumidSensor() {
    
}

std::unique_ptr<DeviceStatus> TempHumidSensor::getStatus() const  {
  return std::make_unique<DeviceStatus> ();
}

TempHumidSensorStatus TempHumidSensor::readSensorData() {
  if(!connect()){
    //串口未打开
    return TempHumidSensorStatus();
  }
  auto command = buildReadDataCommand();
  
  if( !sendData(command)){
    //发送指令失败
    return TempHumidSensorStatus();
  }

  

  unsigned char recvBuf[256];
  int len =recviceData(recvBuf,sizeof(recvBuf),100);
  
  if(len < 0) {
    //未收到响应 超时
    return TempHumidSensorStatus();
  }

  int slaveAddr = std::stoul(this->getSlaveAddr().substr(2),nullptr,16);
  if(len == 9 && recvBuf[0] == slaveAddr && recvBuf[1] == 0x03) {
    float humidity = ( (recvBuf[3] << 8) | recvBuf[4] ) / 10.0;
    float tempature = ((recvBuf[5] << 8) | recvBuf[6] ) / 10.0;
  
    return TempHumidSensorStatus(this->getDeviceId(),1,this->getName(),TempHumidStatus::NORMAL,humidity,tempature);
  } else if( len == 0) {
    //未收到响应
    return TempHumidSensorStatus(this->getDeviceId(),1,this->getName(),TempHumidStatus::ABNORMAL,0.0f,0.0f);
  } else {
    //响应异常
    return TempHumidSensorStatus(this->getDeviceId(),1,this->getName(),TempHumidStatus::ABNORMAL,0.0f,0.0f);
  }
}

bool TempHumidSensor::sendData(const std::array<uint8_t,8>& data) {
  ssize_t sent = write(serialPortStatus_,data.data(),data.size());
  tcdrain(serialPortStatus_);
  return true;
}

int TempHumidSensor::recviceData(unsigned char* buf,int bufSize,int waitTime) {
  std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
  memset(buf,0,sizeof(buf));
  int len = read(serialPortStatus_,buf,bufSize);
  return len;
}

std::array<uint8_t,8> TempHumidSensor::buildReadDataCommand() {
  std::array<uint8_t,8> frame;
  frame[0] = std::stoi(this->getSlaveAddr(),nullptr,16);
  //功能码
  frame[1] = 0x03;
  uint8_t high,low;
  splitRegAddress(high,low);
  frame[2] = high;
  frame[3] = low;
  frame[4] = 0x00;
  frame[5] = 0x02;
  uint16_t crc = buildCalcCRC(frame.data(),6);
  frame[6] = crc & 0xFF;
  frame[7] = ( crc >> 8) & 0xFF;
  return frame;
}

bool TempHumidSensor::connect() {
  if(serialPortStatus_ >= 0) {
    //串口已打开
    return true;
  }

  //初始化串口
  serialPortStatus_ = open(this->getBindSerialPort().c_str(),O_RDWR | O_NOCTTY | O_SYNC);
  if(serialPortStatus_ < 0) return false;//打开失败
  if(!configureSerial() ) {
    close(serialPortStatus_);
    serialPortStatus_ = -1;
    return false;
  }

  //串口初始化成功，并打开
  return true;
}

void TempHumidSensor::disconnect() {
  if(serialPortStatus_ >= 0) {
    close(serialPortStatus_);
    serialPortStatus_ = -1;
    //串口已关闭
  }
}

bool TempHumidSensor::configureSerial() {
  struct termios tty;
  if(tcgetattr(serialPortStatus_,&tty) != 0) {
    //配置失败
    return false;
  }

  speed_t speed = serialConfig_.getBaudRate();
  cfsetospeed(&tty,speed);
  cfsetispeed(&tty,speed);
  //校验位
  serialConfig_.setPlcParity(tty);
  serialConfig_.setPlcStopBits(tty);
  serialConfig_.setPlcDataBits(tty);

  tty.c_cflag &= ~CRTSCTS;
  // 启用串口接收（CREAD）+ 忽略调制解调器状态线（CLOCAL）
  tty.c_cflag |= CREAD | CLOCAL;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
  tty.c_oflag = 0;
  tty.c_lflag = 0;

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10;

  if (tcsetattr(serialPortStatus_, TCSANOW, &tty) != 0) {
      //"串口参数设置失败(tcsetattr)"
      return false;
  }
  return true;
}

void TempHumidSensor::splitRegAddress(uint8_t& high,uint8_t& low) {
  int value = std::stoi(this->getRegAddr(),nullptr,16);
  uint16_t regAddr = static_cast<uint16_t>(value);
  high = ( regAddr >> 8) & 0xFF;
  low = regAddr & 0xFF;
}

uint16_t TempHumidSensor::buildCalcCRC(const uint8_t* data,size_t length) {
  uint16_t crc = 0xFFFF;

  for(size_t i = 0; i < length; ++i){
    crc ^= data[i];

    for(int j = 0; j < 8; ++j) {
      if(crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else crc >>= 1;
    }
  }
  return crc;
}


// SensorStatus SerialDirectSensor::getStatus() {

// }

// SensorRealTimeData SerialDirectSensor::getRealTimeData() {
    
// }