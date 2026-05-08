#pragma once
#include <string>
#include "data_layer/device/device_data_object.h"
#include "data_layer/device/driver/device_driver.h"
#include "data_layer/device/runtime/device_session_context.h"
#include "business_layer/buffer/frame_buffer.h"
#include <thread>
#include <atomic>
struct RuntimeContext {
    std::shared_ptr<FrameBuffer> frameBuffer;

    // std::shared_ptr<FrameBuffer>     frameBuffer;      // 视频帧缓冲
    // std::shared_ptr<VideoDao>        videoDao;         // 视频存储DAO

    // // ===================== 2. 传感器相关 ===================
    // std::shared_ptr<SensorDao>       sensorDao;        // 传感器数据DAO

    // // ===================== 3. 点云相关 =====================
    // std::shared_ptr<PointCloudBuffer> pointCloudBuffer;// 点云缓冲
    // std::shared_ptr<PointCloudDao>   pointCloudDao;    // 点云DAO
};


//父类
class DeviceRuntime{
public:
    virtual ~DeviceRuntime() = default;

    virtual const std::string& getId() const = 0;

    virtual DeviceType getType() const = 0;


    virtual bool isOnline() const = 0;


    virtual bool initialize() = 0;


    virtual void shutdown() = 0;

    virtual bool start() = 0;

    virtual bool stop() = 0;

    virtual std::shared_ptr<IDeviceSessionContext> getSessionContext() = 0;


    virtual void tick(SensorData& sensorData, int intervalSeconds) = 0;


    virtual PollingSnapshot poll(int interval) = 0; 

    virtual bool shouldPoll(std::chrono::steady_clock::time_point now) const = 0;

    virtual void updateNextPollTime(std::chrono::steady_clock::time_point now) = 0;

    virtual int getCurrentInterval() const = 0;

    

};


//传感器
class SensorRuntime: public DeviceRuntime{ 
public:
    SensorRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver);

    ~SensorRuntime();

    const std::string& getId() const override;

    DeviceType getType() const override;


    bool isOnline() const override;


    bool initialize() override;


    void shutdown() override;

    bool start() override;

    bool stop() override;

    std::shared_ptr<IDeviceSessionContext> getSessionContext() override;

    void tick(SensorData& sensorData, int intervalSeconds)override;


    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;
private:

    //判断什么类型的传感器
    SensorKind getSensorKind();

    //温湿度
    void processTemperatureHumidity(SensorData& data , int intervalSeconds);
    //水浸
    void processWaterImmersion(SensorData& data , int intervalSeconds);
     //压力
    void processPressure(SensorData& data , int intervalSeconds);
     //烟感
    void processSmokeDetection(SensorData& data , int intervalSeconds);


    static std::vector<uint8_t> buildReadRequest(uint8_t slave,uint16_t addr,uint16_t count);

    static uint16_t modbus_crc16(const uint8_t* data, size_t len);

    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;

private:
    std::shared_ptr<DeviceNode> node_;

    std::shared_ptr<IDeviceDriver> driver_;

    RuntimeDeviceStatus runtime_status;

    int failCount_ = 0;
    int interval_ = 5; // 默认5秒
    std::chrono::steady_clock::time_point nextPollTime_;

    PollingSnapshot lastSnapshot_;
};

//执行器 依靠plc

class ActuatorRuntime: public DeviceRuntime{ 
public:
    ActuatorRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver);

    ~ActuatorRuntime();

    const std::string& getId() const override;

    DeviceType getType() const override;


    bool isOnline() const override;


    bool initialize() override;


    void shutdown() override;

    bool start() override;

    bool stop() override;

    std::shared_ptr<IDeviceSessionContext> getSessionContext() override;

    void tick(SensorData& sensorData, int intervalSeconds) override;

    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;

private:
    std::shared_ptr<DeviceNode> node_;

    std::shared_ptr<IDeviceDriver> driver_;

    RuntimeDeviceStatus runtime_status;

};


//网络摄像头  
class CameraRuntime: public DeviceRuntime{ 
public:
    CameraRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<FrameBuffer> frameBuffer);


    ~CameraRuntime();

    const std::string& getId() const override;

    DeviceType getType() const override;

    bool isOnline() const override; //状态

    bool initialize() override; //判断driver_和context_是否存在

    void shutdown() override; //关闭driver_

    bool start()override;//取帧

    bool stop()override; //不取帧


    std::shared_ptr<IDeviceSessionContext> getSessionContext() override;

    void tick(SensorData& sensorData, int intervalSeconds) override;

    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;

private:
    std::shared_ptr<DeviceNode> node_;

    std::shared_ptr<IDeviceDriver> driver_;

    RuntimeDeviceStatus runtime_status;

    std::shared_ptr<FrameBuffer> frameBuffer_;

    std::thread worker_;
    std::atomic<bool> running_{false};

};

//  PLC
class PlcRuntime : public DeviceRuntime {
public:
    PlcRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver);

    ~PlcRuntime();


    const std::string& getId() const override;

    DeviceType getType() const override;


    bool isOnline() const override;


    bool initialize() override;


    void shutdown() override;

    bool start() override;

    bool stop() override;

    std::shared_ptr<IDeviceSessionContext> getSessionContext() override;

    void tick(SensorData& sensorData, int intervalSeconds) override;


    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;
private:
    std::shared_ptr<DeviceNode> node_;

    std::shared_ptr<IDeviceDriver> driver_;

    RuntimeDeviceStatus runtime_status;

};

// 雷达
class RadarRuntime : public DeviceRuntime {
public:
    RadarRuntime(std::shared_ptr<DeviceNode> node,std::shared_ptr<IDeviceDriver> driver);

    ~RadarRuntime();

    const std::string& getId() const override;

    DeviceType getType() const override;


    bool isOnline() const override;


    bool initialize() override;


    void shutdown() override;

    bool start() override;

    bool stop() override;

    std::shared_ptr<IDeviceSessionContext> getSessionContext() ;

    void tick(SensorData& sensorData, int intervalSeconds) override;

    PollingSnapshot poll(int interval) ; 

    bool shouldPoll(std::chrono::steady_clock::time_point now) const ;

    void updateNextPollTime(std::chrono::steady_clock::time_point now) ;

    int getCurrentInterval() const ;
private:
    std::shared_ptr<DeviceNode> node_;

    std::shared_ptr<IDeviceDriver> driver_;

    RuntimeDeviceStatus runtime_status;    
};








// // 网关
// class GatewayRuntime : public DeviceRuntime {
// public:
//     GatewayRuntime(const DeviceNode& node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<RuntimeContext>& context);

//     ~GatewayRuntime();

//     const std::string& getId() const override;

//     const std::string& getType() const override;


//     bool isOnline() const override;


//     bool initialize() override;


//     void shutdown() override;

//      bool start() ;

//      bool stop() ;

//      std::shared_ptr<IDeviceSessionContext> getSessionContext() ;
// private:
//     RuntimeDeviceStatus runtime_status;    
// };


//usb摄像头

//海康摄像头
// class HikvisionCameraRuntime : public DeviceRuntime{ 
// public:
//     HikvisionCameraRuntime(const DeviceNode& node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<RuntimeContext>& context);
//     ~HikvisionCameraRuntime();
//     const std::string& getId() const override;
//     const std::string& getType() const override;
//     bool isOnline() const override;
//     bool initialize() override;
//     void shutdown() override;
//      bool start() ;
//      bool stop() ;
//      std::shared_ptr<IDeviceSessionContext> getSessionContext() ;
// private:
//     DeviceDefinition def;
//     RuntimeDeviceStatus runtime_status;
// };

// //海康NVR runtime，包含多个通道
// class HikvisionNvrRuntime: public DeviceRuntime{ 
// public:
//     HikvisionNvrRuntime(const DeviceNode& node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<RuntimeContext>& context);


//     ~HikvisionNvrRuntime();

//     const std::string& getId() const override;

//     const std::string& getType() const override;


//     bool isOnline() const override;


//     bool initialize() override;


//     void shutdown() override;

//      bool start() ;

//      bool stop() ;

//      std::shared_ptr<IDeviceSessionContext> getSessionContext() ;

// private:

//     DeviceDefinition def;
//     // std::shared_ptr<IHikvisionNvrChannelDrivcer> driver;
//     std::shared_ptr<IDeviceSessionContext> nvrSession;

//     RuntimeDeviceStatus runtime_status;
// };

// //海康NVR 运行时，单个通道的runtime，依赖于NVR runtime
// class HikvisionNvrChannelRuntime: public DeviceRuntime{ 
// public:
//     HikvisionNvrChannelRuntime(const DeviceNode& node,std::shared_ptr<IDeviceDriver> driver,std::shared_ptr<RuntimeContext>& context);


//     ~HikvisionNvrChannelRuntime();

//     const std::string& getId() const override;

//     const std::string& getType() const override;


//     bool isOnline() const override;


//     bool initialize() override;


//     void shutdown() override;

//      bool start() ;

//      bool stop() ;

//      std::shared_ptr<IDeviceSessionContext> getSessionContext() ;

// private:

//     DeviceDefinition def;
//     std::shared_ptr<DeviceRuntime> parentRuntime;
//     // std::shared_ptr<IHikvisionNvrChannelDrivcer> driver;
//     std::shared_ptr<IDeviceSessionContext> nvrSession;
//     int channel ;
//     RuntimeDeviceStatus runtime_status;
// };
