#pragma once
#include <map>
#include <string>
#include <stdint.h>
#include <vector>
#include "data_layer/device/device_data_object.h"
enum class DeviceExecStatus {
    Success,
    Failed,
    Timeout,
    Exception
};

class DeviceResult{
public:
   DeviceResult(DeviceExecStatus status = DeviceExecStatus::Success,
                 const std::string& message = "")
        : status_(status), message_(message) {}

    ~DeviceResult() = default;

    bool isOk() const;
    
    DeviceExecStatus getStatus();

    const std::string& getMessage() const;

    uint64_t getTimestamp() const;

    static DeviceResult Ok();

    static DeviceResult Fail(const std::string& msg);

    static DeviceResult Timeout(const std::string& msg = "timeout");

    static DeviceResult Exception(const std::string& msg);
private:
    uint64_t now() const;
private:
    DeviceExecStatus status_;        // 执行状态
    std::string message_;        // 错误信息 / 描述
    uint64_t timestamp_ = now();// 结果产生时间
};



enum class DeviceAction {
    Start,
    Stop,
    Open,
    Close,
    SetValue
};


class DeviceStatusView{

    
};
//这个类就是存放所有控制设备的命令构成 
class DeviceCommand{
public:
    DeviceCommand(const std::string& deviceId,
                  DeviceAction action,
                  int value = 0,
                  const std::string& operatorName = "system")
        : deviceId_(deviceId),
          action_(action),
          value_(value),
          operatorName_(operatorName),
          timestamp_(now())
    {}    

    static DeviceCommand Start(const std::string& deviceId);

    static DeviceCommand Stop(const std::string& deviceId) ; 

    static DeviceCommand Open(const std::string& deviceId);

    static DeviceCommand Close(const std::string& deviceId);

    static DeviceCommand SetValue(const std::string& deviceId, int value);

    const std::string& getDeviceId() const;
     
    DeviceAction getAction() const;
    
    int getValue() const;

    const std::string& getOperatorName() const;
    
    uint64_t getTimestamp() const;

    const std::map<std::string, std::string>& getExtraParams() const ;
    
    void addParam(const std::string& key, const std::string& value);


private:
    static uint64_t now();


private:
    std::string deviceId_;        // 设备唯一ID
    DeviceAction action_;         // 操作类型

    int value_ = 0;               // 可选参数（如阀门开度、频率等）

    std::string operatorName_;    // 操作人（系统 / 用户）
    uint64_t timestamp_ = 0;      // 时间戳（ms）

    // 可扩展（很重要）
    std::map<std::string, std::string> extraParams_;
};

class DeviceStatusQuery{


};





class RecoveryRequest{

};




struct CameraKeyFrame {
    std::string cameraId;        // 摄像头唯一标识
    std::vector<uint8_t> frameData; // 关键帧二进制数据
    uint64_t timestamp;          // 关键帧时间戳
    bool isSuccess;              // 获取是否成功
};

// 雷达数据结构体（根据业务自定义）
struct RadarData {
    std::string radarId;         // 雷达唯一标识
    float distance;               // 检测距离
    float angle;                  // 检测角度
    uint64_t timestamp;           // 数据时间戳
};