#pragma once
#include <string>
#include <vector>
#include "data_layer/device/device_data_object.h"
#include "data_layer/device/vendor_sdk/vendor_sdk.h"
#include "data_layer/device/topology/topology_builder.h"

#include "data_layer/device/frame_data_object.h"
#include <mutex>
//串口
#include <termios.h>
#include <stdio.h>    // 标准IO
#include <fcntl.h>    // open() 函数
#include <unistd.h>   // close()、read()、write()
#include <errno.h>    // 错误码

class  IDeviceDriver{
public:

    virtual ~IDeviceDriver() = default;

    virtual bool connect() = 0;

    virtual bool disconnect() = 0;

    virtual bool isConnected() = 0;

    virtual std::string driverName() const = 0 ;


    virtual bool fetchFrame(std::shared_ptr<FrameData>& outFrame) = 0; //流

    virtual bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms)  = 0 ;
};

class RtspFFmpegDriver : public IDeviceDriver {
public:
    RtspFFmpegDriver(std::shared_ptr<ISdkEnvironment> sdkEnv ,const DeviceNode& node)
        : sdkEnv_(std::move(sdkEnv)),
            node_(node) 
        {}

    ~RtspFFmpegDriver();    

    bool connect() override;

    bool disconnect() override ;

    bool isConnected() override ;

    std::string driverName() const override ;
    
    bool fetchFrame(std::shared_ptr<FrameData>& outFrame) override;

    bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms) override ;
private:
    void releaseResources();

    bool readPacket(AVPacket* packet);

    bool decodePacket(const AVPacket* packet, AVFrame* frame);

private:

    std::shared_ptr<ISdkEnvironment> sdkEnv_;

    DeviceNode node_;
    bool connected_ = false;
    // ========= 状态 =========
    int videoStreamIndex_ = -1;

    // ========= FFmpeg：解封装 =========
    AVFormatContext* fmtCtx_ = nullptr;

    // ========= FFmpeg：解码 =========
    AVCodecContext* codecCtx_ = nullptr;

    // ========= FFmpeg：数据 =========
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
};


class HikvisionNvrDriver : public IDeviceDriver {
public:
    HikvisionNvrDriver(
        std::shared_ptr<ISdkEnvironment> sdkEnv,DeviceNode node)
        : sdkEnv_(std::move(sdkEnv)),
          node_(std::move(node)), userId_(-1), connected_(false){}

    ~HikvisionNvrDriver();
    
    bool connect() override;

    bool disconnect() override;

    bool isConnected()  override ;  

    std::string driverName() const override ;

    bool fetchFrame(std::shared_ptr<FrameData>& outFrame) override; //流

    bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms) override ;
private:
    std::shared_ptr<ISdkEnvironment> sdkEnv_;
    DeviceNode node_;
    int64_t userId_ = -1; 
    
    bool connected_ = false;
};

class HikvisionChannelDriver {

};    

class ModbusTcpDriver : public IDeviceDriver {
public:
    ModbusTcpDriver(const DeviceNode& node)
        : node_(node) {}

    ~ModbusTcpDriver();    
    bool connect() override ;

    bool disconnect() override ;

    bool isConnected() override ;

    std::string driverName() const override ;

    int readRegister(int addr);

    bool writeRegister(int addr, int value);

    bool fetchFrame(std::shared_ptr<FrameData>& outFrame) override; //流

    bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms) override ;

private:
    DeviceNode node_;
    bool connected_ = false;
};

class ModbusRtuDriver : public IDeviceDriver { // 不要写 CRC 代码里自动生成
public:

    ModbusRtuDriver(const InterfaceDefinition& iface)
        : interface_(iface) , connected_(false), serial_fd_(-1){}
    
    ~ModbusRtuDriver();

    bool connect() override;

    bool disconnect() override ;

    bool isConnected() override ;

    std::string driverName() const override ;

    bool fetchFrame(std::shared_ptr<FrameData>& outFrame) override; //流

    bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms) override ;

private:
    static uint16_t modbus_crc16(const uint8_t* data, size_t len);
    static speed_t baudToFlag(int baud);
    static bool configureSerial(int fd, const InterfaceDefinition& iface);

private:
    InterfaceDefinition interface_;
    bool connected_ = false;
    int serial_fd_ = -1;
    std::mutex io_mutex_;
};

class RadarLqhDriver : public IDeviceDriver {
public:
    RadarLqhDriver(const DeviceNode& node)
        : node_(node) {}

    ~RadarLqhDriver();


    bool connect() override ;

    bool disconnect() override;

    bool isConnected() override;
    std::string driverName() const override ;

    bool readPacket(std::vector<uint8_t>& data);

    bool fetchFrame(std::shared_ptr<FrameData>& outFrame) override; //流

    bool transact(const std::vector<uint8_t>& request,std::vector<uint8_t>& response,int timeout_ms) override ;
private:
    DeviceNode node_;
    bool connected_ = false;
    int serial_fd_;      // 串口文件描述符（Linux）
};

