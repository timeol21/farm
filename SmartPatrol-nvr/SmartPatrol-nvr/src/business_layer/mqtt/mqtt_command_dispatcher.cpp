#include "mqtt_command_dispatcher.h"
#include "mqtt_topics.h"
// const std::string GET_REAL_IMAGE_TOPIC = "device/camera/getRealImage";
// const std::string OPERATE_PLC_TOPIC = "device/plc/operate";
// const std::string UPDATE_CONFIG_TOPIC = "device/config/update";
// const std::string GET_SENSOR_DATA_TOPIC = "device/sensor/status";
// const std::string GET_ALL_DEVICE_STATUS_TOPIC = "device/status/getall";

MqttCommandDispatcher::MqttCommandDispatcher(JobScheduler& scheduler)
    :scheduler_(scheduler){}
void MqttCommandDispatcher::onMessage(const std::string& topic, const std::string& payload)
{
    nlohmann::json j;
    try{
        j = nlohmann::json::parse(payload);
    }catch(...){
        std::cerr << "Invalid JSON: " << payload << std::endl;
        return;
    }
    // 用 MQTT topic 决定任务类型
    if (topic == GET_REAL_IMAGE_TOPIC) {
        handleGetRealImage(j);
    }
    else if (topic == OPERATE_PLC_TOPIC) {
        handleOperatePlc(j);
    }
    else if (topic == UPDATE_CONFIG_TOPIC) {
        handleUpdateConfig(j);
    }
    // else if(topic == GET_SENSOR_DATA_TOPIC) {
    //     handleGetSensorData(j);
    // }
    else if (topic == OPERATE_CAR_TOPIC) {
        handleOperateCar(j);
    }
    else if(topic == GET_ALL_DEVICE_STATUS_TOPIC){
        handleGetAllDeviceStatus(j);
    }
    else if(topic == OPERATE_PLC_WITH_VERIFY_TOPIC){
        handleOperatePlcWithVerify(j);
    }
    else if(topic == UPDATE_CONFIG){
        handleConfigUpdate(j);
    }
    else if(topic == GET_VIDEO_HISTORY_TOPIC ){
        handVideoHistory(j);
    }
    else if(topic == GET_VIDEO_HISTORY_FILE_TOPIC){
        handVideoHistoryFile(j);
    }

    else {
        std::cout << "Unknown topic: " << topic << std::endl;
    }
}

void MqttCommandDispatcher::handleGetRealImage(const nlohmann::json& j)
{
    if (!j.contains("deviceId")) return;
    std::string nvrId = j["nvrId"];
    std::string camId = j["deviceId"];

    
    auto task = std::make_shared<GetCameraRealImageTask>(camId,nvrId);
    int id = scheduler_.submit(task, "mqtt");

    std::cout << "Submitted GetRealImageTask id=" << id 
              << " for cam=" << camId << std::endl;
}
void MqttCommandDispatcher::handleOperatePlc(const nlohmann::json& j)
{
    if(!j.contains("deviceId") || !j.contains("action")) return;
    std::string deviceId = j["deviceId"];
    std::string cmd = j["action"];
    auto task = std::make_shared<OperateValveTask>(deviceId, cmd);
    int id = scheduler_.submit(task, "mqtt");

    std::cout << "Submitted OperatePLC id=" << id 
              << " for device=" << deviceId << " operation=" << cmd << std::endl;
}
// void MqttCommandDispatcher::handleGetPLCDeviceStatus(const nlohmann::json& j){
//     if(!j.contains("deviceId")) return;
//     std::string deviceId = j["deviceId"];
//     auto task = std::make_shared<GetPLCDeviceTask>(deviceId);
//     int id = scheduler_.submit(task, "mqtt");

//     std::cout << "Submitted GetPLCDeviceStatus id=" << id 
//               << " for device=" << deviceId << std::endl;
// }
// void MqttCommandDispatcher::handleGetSensorData(const nlohmann::json& j)
// {
//     if(!j.contains("sensorId")) return;
//     std::string sensorId = j["sensorId"];
//     auto task = std::make_shared<GetSensorDataTask>(sensorId);
//     int id = scheduler_.submit(task, "mqtt");

//     std::cout << "Submitted GetSensorDataTask id=" << id 
//               << " for sensor=" << sensorId << std::endl;
// }
void MqttCommandDispatcher::handleUpdateConfig(const nlohmann::json& j)
{

}

void MqttCommandDispatcher::handleOperateCar(const nlohmann::json& j)
{
//     if (!j.contains("carcontrol_id") && !j.contains("motor1") && !j.contains("motor2")) {
//         std::cerr << "Invalid car control command: missing required fields" << std::endl;
//         return;
//     }
    
//     std::string carId = j.value("carcontrol_id", std::string("carcontrol001"));
//     int motor1 = j.value("motor1", 0);
//     int motor2 = j.value("motor2", 0);
    
//     // 参数范围检查
//     motor1 = std::max(-1500, std::min(1500, motor1));
//     motor2 = std::max(-1500, std::min(1500, motor2));
    
//     std::cout << "Handling car control command for car_id: " << carId 
//               << ", motor1: " << motor1 << ", motor2: " << motor2 << std::endl;
    
//     // 创建并提交小车控制任务
//     auto task = std::make_shared<CarControlTask>(j);
//     int id = scheduler_.submit(task, "mqtt");
    
//     std::cout << "Submitted CarControlTask id=" << id 
//               << " for car=" << carId 
//               << " motor1=" << motor1 
//               << " motor2=" << motor2 << std::endl;
}

void MqttCommandDispatcher::handleGetAllDeviceStatus(const nlohmann::json& j)
{
    auto task = std::make_shared<GetDeviceStatusTask>();
    int id = scheduler_.submit(task, "mqtt");

    std::cout << "submitted GetDeviceStatusTask id=" << id << std::endl;
}

void MqttCommandDispatcher::handleOperatePlcWithVerify(const nlohmann::json& j){
    // if(!j.contains("deviceId") || !j.contains("action") || !j.contains("sensorId") || !j.contains("cameraId")) return;
    // std::string deviceId = j["deviceId"];
    // std::string cmd = j["action"];
    // std::string sensorId = j["sensorId"];
    // std::string cameraId = j["cameraId"];
    // auto task =  std::make_shared<OperateValueWithVerifyTask>(deviceId, cmd, sensorId, cameraId);
    // int id = scheduler_.submit(task, "mqtt");

    // std::cout << "Submitted OperatePlcWithVerify id=" << id 
    //           << " for device=" << deviceId << " operation=" << cmd 
    //           << " sensor=" << sensorId << " camera=" << cameraId <<std::endl;
}

void MqttCommandDispatcher::handleConfigUpdate(const nlohmann::json& j){
    // if(!j.contains("new_config_data")) return;
    // std::string newConfigJson = j.at("new_config_data").get<std::string>();
    
    // auto task = std::make_shared<UpdateConfigTask>(newConfigJson);
    // int id = scheduler_.submit(task, "mqtt");

    // std::cout<<"Submitted UpdateConfigTask id=" <<id<<std::endl;
}


void MqttCommandDispatcher::handVideoHistory(const nlohmann::json& j){
    if(!j.contains("deviceId") && !j.contains("nvrId")) return;
    std::string deviceId = j["deviceId"];
    std::string nvrId = j["nvrId"];
    std::string startTime = j["startTime"];
    std::string endTime = j["endTime"];
    auto task = std::make_shared<GetVideoHistoryTask>(deviceId,nvrId,startTime,endTime);
    int id = scheduler_.submit(task, "mqtt");
    std::cout << "handVideoHistory id=" << id 
              << " for device=" << deviceId << " nvrId=" << nvrId << std::endl;
}

void MqttCommandDispatcher::handVideoHistoryFile(const nlohmann::json& j){

}