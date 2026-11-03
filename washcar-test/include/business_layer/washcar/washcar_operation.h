#pragma once

// 洗车命令的入参 / 出参。
//
// 这两个类原来定义在 business_layer/lobby/lobby_object.h 里，和一大堆
// 摄像头/传感器/雷达的查询对象混在一起。washcar-test 只保留洗车主链路，
// 所以把它们单独抽出来，避免把整个 lobby 层（1446 行）拖进来。

#include <nlohmann/json.hpp>

#include "common/erro_code.h"

#include <string>
#include <unordered_map>

using json = nlohmann::json;

/// 一条洗车命令的参数（由 MQTT payload 解析而来）
class WashCarOperation {
public:
    WashCarOperation() = default;
    explicit WashCarOperation(const json& j);
    ~WashCarOperation() = default;

    std::string getBoxNo() const;
    std::string getPlcId() const;
    std::string getMode() const;
    std::string getPoint() const;
    std::string getReqSource() const;
    std::string getTime() const;
    std::string getProductId() const;
    std::string getDeviceNum() const;
    const std::unordered_map<std::string, int>& getQueryPoints() const;

    bool isValid() const;

    void setReqSource(const std::string& reqSource_);
    void setBoxNo(const std::string& boxNo_);
    /// 云端 payload 不带 plcId 时，由路由层补上本机型实际在用的 PLC 标识
    void setPlcId(const std::string& plcId_);
    void setProductId(const std::string& id);
    void setDeviceNum(const std::string& num);
    void addQueryPoint(const std::string& name, int value);

    /// 点位解析辅助：point 形如 "M100" / "X45" / "Y74"
    int getPointDecimal() const;
    std::string getPointType() const;

private:
    std::string boxNo;
    std::string plcId;
    std::string mode;
    std::string point;
    std::string reqSource;
    std::string time;
    std::string productId = "93";
    std::string deviceNum = "11";
    std::unordered_map<std::string, int> queryPoints;
};

/// 一条洗车命令的执行结果
class WashCarOperationResult {
public:
    WashCarOperationResult() = default;
    WashCarOperationResult(const std::string& boxNo_, ErrorCode::Code code_, const std::string& message_)
        : boxNo(boxNo_), code(code_), message(message_) {}
    ~WashCarOperationResult() = default;

    static WashCarOperationResult createResult(bool success, const WashCarOperation& operation) {
        if (success) {
            return WashCarOperationResult(operation.getBoxNo(), ErrorCode::Code::SUCCESS, "success");
        }
        return WashCarOperationResult(operation.getBoxNo(),
                                      ErrorCode::Code::WASHCAR_PLC_COMMAND_FAILED, "fault");
    }

    std::string getBoxNo() const { return boxNo; }
    ErrorCode::Code getCode() const { return code; }
    std::string getMessage() const { return message; }

    void setBoxNo(const std::string& v) { boxNo = v; }
    void setCode(ErrorCode::Code v) { code = v; }
    void setMessage(const std::string& v) { message = v; }

    int getCurrentStep() const { return currentStep; }
    void setCurrentStep(int v) { currentStep = v; }

    std::string getFaultCode() const { return faultCode; }
    void setFaultCode(const std::string& v) { faultCode = v; }

    std::string getFaultDescription() const { return faultDescription; }
    void setFaultDescription(const std::string& v) { faultDescription = v; }

    bool isCompleted() const { return code == ErrorCode::Code::SUCCESS && currentStep == 10; }
    bool hasFault() const { return code != ErrorCode::Code::SUCCESS; }
    bool isStopped() const { return code == ErrorCode::Code::WASHCAR_STOPPED; }

    void addStatusPoint(const std::string& name, int value) { status[name] = value; }
    void clearStatusPoints() { status.clear(); }
    const std::unordered_map<std::string, int>& getStatus() const { return status; }

    std::string toJson() const {
        json j;
        j["boxNo"] = boxNo;
        j["code"] = static_cast<int>(code);
        j["message"] = message;
        j["currentStep"] = currentStep;
        j["faultCode"] = faultCode;
        j["faultDescription"] = faultDescription;
        j["status"] = status;
        return j.dump();
    }

private:
    std::string boxNo;
    ErrorCode::Code code{ErrorCode::Code::SUCCESS};
    std::string message;
    int currentStep{0};
    std::string faultCode;
    std::string faultDescription;
    std::unordered_map<std::string, int> status;
};
