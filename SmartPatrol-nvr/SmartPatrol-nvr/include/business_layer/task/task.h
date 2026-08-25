#ifndef TASK_H
#define TASK_H

#include "itask.h"
#include <iostream>
#include <thread>
#include "json.hpp"
class GetCameraRealImageTask : public ITask
{
public:
    GetCameraRealImageTask(std::string camId,std::string nvrId)
        :camId_(camId),nvrId_(nvrId){}
    std::string name() const override{return "GetCameraRealImage";}
    void run(TaskContext& ctx)override;

private:
    std::string camId_;
    std::string nvrId_;
}; 

class OperateValveTask : public ITask
{
public:
    OperateValveTask(std::string deviceId, std::string cmd)
        : deviceId_(deviceId), cmd_(cmd){}
    std::string name() const override { return "OperateValve"; }
    void run(TaskContext& ctx) override;
private:
    std::string deviceId_;
    std::string cmd_;
};

class GetPLCDeviceTask : public ITask
{
public:
    GetPLCDeviceTask(std::string deviceId)
        : deviceId_(deviceId) {}
    std::string name() const override { return "GetPLCDeviceStatus"; }
    void run(TaskContext& ctx) override;
private:
    std::string deviceId_;
};

class GetSensorDataTask : public ITask
{
public:
    GetSensorDataTask(std::string sensorId)
        : sensorId_(sensorId) {}
    std::string name() const override { return "GetSensorData"; }
    void run(TaskContext& ctx) override;
private:
    std::string sensorId_;
};

class CarControlTask : public ITask
{
public:
    CarControlTask(const nlohmann::json& payload)
        : payload_(payload) {}
    std::string name() const override { return "CarControlTask"; }
    void run(TaskContext& ctx) override;

private:
    nlohmann::json payload_;
    
    // 添加辅助方法
    bool validatePayload(const nlohmann::json& payload);
    void publishResult(ITaskResultPublisher* publisher, const nlohmann::json& result);
};

class GetDeviceStatusTask : public ITask
{
public:
    GetDeviceStatusTask() {}
    std::string name() const override { return "GetDeviceStatus";}
    void run(TaskContext& ctx) override;
};

class GetVideoHistoryTask : public ITask{
public:
    GetVideoHistoryTask(std::string camId,std::string nvrId,std::string startTime,std::string endTime):camId_(camId),nvrId_(nvrId),startTime_(startTime),endTime_(endTime){}
    std::string name() const override { return "GetVideoHistory"; }
    void run(TaskContext& ctx) override;
private:
    std::string camId_;
    std::string nvrId_;
    std::string startTime_;
    std::string endTime_; 
};


class GetVideoHistoryFileTask : public ITask{
public: 
     GetVideoHistoryFileTask(std::string camId,std::string nvrId, std::string fileName,std::string startTime,std::string endTime,std::string fileSize):camId_(camId),nvrId_(nvrId),fileName_(fileName),startTime_(startTime),endTime_(endTime),fileSize_(fileSize){}
    std::string name() const override { return "GetVideoHistoryFile"; }
    void run(TaskContext& ctx) override;
private:
    std::string camId_;
    std::string nvrId_ ;
    std::string fileName_;
    std::string startTime_;
    std::string endTime_ ;
    std::string fileSize_;
};

// 门锁控制任务（仅开锁）
class OpenDoorLockTask : public ITask
{
public:
    OpenDoorLockTask(const std::string& lockId)
        : lockId_(lockId) {}
    std::string name() const override { return "OpenDoorLock"; }
    void run(TaskContext& ctx) override;
private:
    std::string lockId_;
};

// 传感器报警任务（当传感器异常时上传数据和状态）
class SensorAlarmTask : public ITask
{
public:
    // alarmType: 报警类型 (如 "infrared", "water_immersion", "smoke", "temperature_humidity")
    // sensorId: 传感器ID
    // alarmReason: 报警原因描述
    // sensorData: 传感器相关数据（可选，如温度、湿度等）
    SensorAlarmTask(const std::string& alarmType, 
                    const std::string& sensorId,
                    const std::string& alarmReason,
                    const nlohmann::json& sensorData = nlohmann::json::object())
        : alarmType_(alarmType)
        , sensorId_(sensorId)
        , alarmReason_(alarmReason)
        , sensorData_(sensorData) {}
    
    std::string name() const override { return "SensorAlarm"; }
    void run(TaskContext& ctx) override;

private:
    std::string alarmType_;      // 报警类型
    std::string sensorId_;       // 传感器ID
    std::string alarmReason_;    // 报警原因
    nlohmann::json sensorData_;  // 传感器数据（温度、湿度、触发状态等）
};



#endif