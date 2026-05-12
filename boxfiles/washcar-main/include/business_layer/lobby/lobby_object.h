#pragma once
#include <nlohmann/json.hpp>
#include "common/erro_code.h"
#include <optional>
#include "data_layer/device/device_operation_result.h"

using json = nlohmann::json;
template <typename T>
class LobbyResult{
public:
    bool success; // 操作是否成功
    ErrorCode::Code errorCode; // 错误码，成功时为 ErrorCode::Code::SUCCESS
    std::string message; // 相关信息的字符串
    std::optional<T> data; // 可选的数据字段，成功时包含返回数据，失败时为 std::nullopt

public:  
    
    static LobbyResult<T> Ok(const T& data){// 成功结果，包含数据
        return {true,ErrorCode::Code::SUCCESS,"success",data};
    } 

    static LobbyResult<T>Error(ErrorCode::Code code){//错误结果，包含错误码和对应的错误信息
        return {false,code,ErrorCode::getMessage(code),std::nullopt};
    } 
};

template <>
class LobbyResult<void>{
public:
    bool success; // 操作是否成功
    ErrorCode::Code errorCode; // 错误码，成功时为 ErrorCode::Code::SUCCESS
    std::string message; // 相关信息的字符串
    

public:  
    
    static LobbyResult<void> Ok(){// 成功结果，包含数据
        return {true,ErrorCode::Code::SUCCESS,"success"};
    } 

    static LobbyResult<void>Error(ErrorCode::Code code){//错误结果，包含错误码和对应的错误信息
        return {false,code,ErrorCode::getMessage(code)};
    } 
};



class DeviceStatusQuery{
public:
    explicit DeviceStatusQuery(const json& j){
        // 从JSON对象中提取查询参数并初始化成员变量
        // 例如：
       
    }

    static DeviceStatusQuery createMqttObject(const json& j);

    static DeviceStatusQuery createHttpObject(const json& j);

    bool isValid() const {
        // 实现查询参数的验证逻辑，例如检查必需的字段是否存在，值是否在合理范围内等
        return true; // 返回true表示查询有效，false表示无效
    } 

    std::string getReqSource() const {
        return reqSource;
    };

private:
    std::string reqSource;



private:
    
};
class SensorQuery {

};
class EnvironmentQuery {

};
class FrameQuery {
public:
    explicit FrameQuery(const nlohmann::json& j){
        // 从JSON对象中提取查询参数并初始化成员变量
        // 例如：
        // cameraId = j.at("cameraId").get<std::string>();
        // timestamp = j.at("timestamp").get<std::string>();
    }


    bool isValid() const {
        // 实现查询参数的验证逻辑，例如检查必需的字段是否存在，值是否在合理范围内等
        return true; // 返回true表示查询有效，false表示无效
    } 
private:
    // std::string cameraId;
    // std::string timestamp;       
};
class HistoricalVideoQuery {
public:
    explicit HistoricalVideoQuery(const nlohmann::json& j){
        // 从JSON对象中提取查询参数并初始化成员变量
        // 例如：
        // cameraId = j.at("cameraId").get<std::string>();
        // timestamp = j.at("timestamp").get<std::string>();
    }


    bool isValid() const {
        // 实现查询参数的验证逻辑，例如检查必需的字段是否存在，值是否在合理范围内等
        return true; // 返回true表示查询有效，false表示无效
    } 
private:
    // std::string cameraId;
    // std::string timestamp; 
};
class AlarmQuery {
public:
    explicit AlarmQuery(const nlohmann::json& j){
        // 从JSON对象中提取查询参数并初始化成员变量
        // 例如：
        // cameraId = j.at("cameraId").get<std::string>();
        // timestamp = j.at("timestamp").get<std::string>();
    }


    bool isValid() const {
        // 实现查询参数的验证逻辑，例如检查必需的字段是否存在，值是否在合理范围内等
        return true; // 返回true表示查询有效，false表示无效
    } 
private:
    // std::string cameraId;
    // std::string timestamp; 
};
class DownloadHistoricalVideo {
public:
    explicit DownloadHistoricalVideo(const nlohmann::json& j){
        // 从JSON对象中提取查询参数并初始化成员变量
        // 例如：
        // cameraId = j.at("cameraId").get<std::string>();
        // timestamp = j.at("timestamp").get<std::string>();
    }


    bool isValid() const {
        // 实现查询参数的验证逻辑，例如检查必需的字段是否存在，值是否在合理范围内等
        return true; // 返回true表示查询有效，false表示无效
    } 
private:
    // std::string cameraId;
    // std::string timestamp; 
};
class DoorLockOperation {

};
class SolenoidValveOperation {
public:
    SolenoidValveOperation(const json& j);
    ~SolenoidValveOperation() = default;
    const std::string& getDeviceId() const;
    const std::string& getCmd() const;
    const std::string& getPlcId() const;
    const std::string& getReqSource() const;
    bool isValid();
private:
    std::string deviceId;
    std::string cmd;
    std::string plcId;
    std::string reqSource;
};

class SolenoidValveOperationResult {
public: 
    SolenoidValveOperationResult(const std::string& deviceId_, const std::string& plc_, int code_, const std::string& message_): deviceId(deviceId_), plc(plc_), code(code_), message(message_) {}
    ~SolenoidValveOperationResult() = default;
    static SolenoidValveOperationResult createResult(const DeviceOperationResult& result , const SolenoidValveOperation& operation ){
        return SolenoidValveOperationResult(
            operation.getDeviceId(),                
            operation.getPlcId(),                
            result.operationBool() ? 0 : -1,  
            result.getMessage()
        );
    }
    std::string getDeviceId() const { return deviceId; }
    std::string getPlc() const { return plc; }
    int getCode() const { return code; }
    std::string getMessage() const { return message; }
private:
    std::string deviceId;
    std::string plc;
    int code;
    std::string message;

};

class WashCarOperation {
public:
    WashCarOperation() = default;
    explicit WashCarOperation(const json& j);
    ~WashCarOperation() = default;

    std::string getBoxNo() const;
    std::string getPlcId() const;
    std::string getMode() const;
    std::string getReqSource() const;
    std::string getTime() const;
    bool isValid() const;

private:
    std::string boxNo;
    std::string plcId;
    std::string mode;
    std::string reqSource;
    std::string time;
};

class WashCarOperationResult {
public:
    WashCarOperationResult() = default;
    WashCarOperationResult(const std::string& boxNo_, int code_, const std::string& message_)
        : boxNo(boxNo_), code(code_), message(message_) {}
    ~WashCarOperationResult() = default;

    static WashCarOperationResult createResult(bool success, const WashCarOperation& operation) {
        if (success) {
            return WashCarOperationResult(operation.getBoxNo(), 0, "success");
        } else {
            return WashCarOperationResult(operation.getBoxNo(), -1, "fault");
        }
    }

    std::string getBoxNo() const { return boxNo; }
    int getCode() const { return code; }
    std::string getMessage() const { return message; }

    void setBoxNo(const std::string& v) { boxNo = v; }
    void setCode(int v) { code = v; }
    void setMessage(const std::string& v) { message = v; }


    int getCurrentStep() const { return currentStep; }
    void setCurrentStep(int v) { currentStep = v; }

    std::string getFaultCode() const { return faultCode; }
    void setFaultCode(const std::string& v) { faultCode = v; }

    std::string getFaultDescription() const { return faultDescription; }
    void setFaultDescription(const std::string& v) { faultDescription = v; }

    bool isCompleted() const {
        return code == 0 && currentStep == 10;
    }

    bool hasFault() const {
        return code == -1;
    }

    bool isStopped() const {
        return code == -2;
    }

    std::string toJson() const {
        json j;
        j["boxNo"] = boxNo;
        j["code"] = code;
        j["message"] = message;
        j["currentStep"] = currentStep;
        j["faultCode"] = faultCode;
        j["faultDescription"] = faultDescription;
        return j.dump();
    }

private:
    std::string boxNo;
    int code;
    std::string message;
    int currentStep{0};
    std::string faultCode;
    std::string faultDescription;
};

class CameraOperation {

};
class CameraConfiguration {

};
class BoxConfiguration {

};
class AIModelDeploy {

};
class AIModelEnable {

};
class AIModelDisable {

};
class AIModelUpdate {

};


#include <string>
#include <memory>
#include <chrono>

struct VideoFrame {
    // ===== 视频数据 =====
    // std::shared_ptr<cv::Mat> image;

    // ===== 元数据 =====
    std::string cameraId;
    std::string nvrId;

    std::chrono::system_clock::time_point timestamp;

    uint64_t frameIndex{0};
    bool isKeyFrame{false};

    // ===== 扩展空间 =====
    // 可扩展AI识别结果、报警标记等

    VideoFrame() = default;

    // VideoFrame(/*std::shared_ptr<cv::Mat> img,
    //            std::string camId,
    //            std::string nvr,
    //            uint64_t index,
    //            bool key*/)
    //     // : image(std::move(img)),
    //     //   cameraId(std::move(camId)),
    //     //   nvrId(std::move(nvr)),
    //     //   timestamp(std::chrono::system_clock::now()),
    //     //   frameIndex(index),
    //     //   isKeyFrame(key)
    // {}
};