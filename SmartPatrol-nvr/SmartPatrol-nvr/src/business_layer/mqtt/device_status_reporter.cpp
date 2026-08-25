#include "device_status_reporter.h"
#include "device_info.h"
#include <chrono>
#include <iostream>
#include <json.hpp>
DeviceStatusReporter::DeviceStatusReporter(IDeviceManager* devMgr,
                                           ITaskResultPublisher* publisher)
    : devMgr_(devMgr), publisher_(publisher) {}

DeviceStatusReporter::~DeviceStatusReporter() {
    stopAutoReport();
}

void DeviceStatusReporter::startAutoReport(const std::string& topic, int intervalSec) {
    if (running_) return;

    topic_ = topic;
    intervalSec_ = intervalSec;
    running_ = true;

    worker_ = std::thread(&DeviceStatusReporter::autoReportLoop, this);
}

void DeviceStatusReporter::stopAutoReport() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void DeviceStatusReporter::autoReportLoop() {
    while (running_) {
        reportStatus(topic_);
        std::this_thread::sleep_for(std::chrono::seconds(intervalSec_));
    }
}

void DeviceStatusReporter::reportStatus(const std::string& topic)
{
    DeviceStatus status = devMgr_->getStatus();
    nlohmann::json j;
     // 3. 构造JSON响应（规范结构）
    j["code"] = 0;          // 0=成功，非0=失败
    j["msg"] = "success";   // 状态描述
    auto& device = j["data"]["device"]; // 规范的data包裹层
    // 4. 处理NVR状态（优先从第一个摄像头取nvrId，若无摄像头则设为空）
    std::string nvrId;
    const auto& cameraList = status.cameraStatusList.getCameraStatusList();
    if (!cameraList.empty()) {
        nvrId = cameraList.front().getNvrId(); // 取第一个摄像头的NVR ID
    }
    // 5. 填充NVR信息
    device["nvr"]["nvrId"] = nvrId;
    device["nvr"]["nvrStatus"] = status.cameraStatusList.getNvrStatus() ? "online" : "offline"; // 转为易读字符串
    // 6. 填充摄像头列表
    nlohmann::json cameraArray = nlohmann::json::array();
    for (const auto& camera : cameraList) {
        nlohmann::json camJson;
        camJson["cameraId"] = camera.getCameraId();
        camJson["nvrId"] = camera.getNvrId();
        // 将CameraStatus枚举转为易读字符串（适配前端）
        switch (camera.getStatus()) {
            case CameraStatus::ONLINE:
                camJson["onlineStatus"] = "online";
                break;
            case CameraStatus::RUNNING:
                camJson["onlineStatus"] = "running";
                break;
            case CameraStatus::OFFLINE:
                camJson["onlineStatus"] = "offline";
                break;
            default:
                camJson["onlineStatus"] = "unknown";
                break;
        }

        cameraArray.push_back(camJson);
    }
    device["cameras"] = cameraArray;
/*
    // PLC
    for (const auto& plc : status.plcStatus_.plcList) {
        for (const auto& d : plc.deviceStatuses) {
            device["plc_device"].push_back({
                {"deviceId", d.id},
                {"name",    d.name},
                {"status",  d.status}
            });
        }
    }

    // Sensors
    for (const auto& sensor : status.sensorStatus_.sensors) {
        nlohmann::json s;
        s["id"] = sensor.id;
        s["type"] = sensor.type;
        s["status"] = to_string(sensor.status);

        if (sensor.status == SensorStatus::NORMAL) {
            if (sensor.type == "modbus") {
                s["temperature"] = sensor.temperature;
                s["humidity"] = sensor.humidity;
            } else {
                s["value"] = sensor.value;
            }
        } else {
            s["code"] = "no data";
        }

        device["sensor"].push_back(s);
    }
*/

    nlohmann::json sensors = nlohmann::json::array();
    for (const auto& sensor : status.sensorStatus.sensors) {
        nlohmann::json s;
        s["sensorId"] = sensor.sensor_id;
        s["type"] = sensor.sensor_type;
        s["isValid"] = sensor.is_valid;
        
        if (sensor.sensor_type == "temperature_humidity") {
            s["temperature"] = sensor.temperature;
            s["humidity"] = sensor.humidity;
        } else {
            s["triggered"] = sensor.triggered;
        }
        sensors.push_back(s);
    }
    device["sensors"] = sensors;
    
    publisher_->publish(topic, j.dump());
}
