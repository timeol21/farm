#include "http_service.h"
#include "mqtt_topics.h"
// ===================== HTTPCommandController 实现 =====================
// 修复：构造函数初始化scheduler_（需确保JobScheduler是单例/可引用）
HTTPCommandController::HTTPCommandController(JobScheduler& scheduler)
    : scheduler_(scheduler) { // 假设JobScheduler有单例接口
}

void HTTPCommandController::onMessage(const std::string& topic, const std::string& payload) {
    try {
        nlohmann::json req_json = nlohmann::json::parse(payload);
        // 根据topic分发到不同的处理函数
        if (topic == GET_REAL_IMAGE_TOPIC) {
            handleGetRealImage(req_json);
        }else if (topic == "/sensor/data") {
            handleGetSensorData(req_json);
        } else if (topic == GET_ALL_DEVICE_STATUS_TOPIC) {
            handleGetAllDeviceStatus(req_json);

        } else if(topic == GET_VIDEO_HISTORY_TOPIC){
            handVideoHistory(req_json);
        } else if(topic == GET_VIDEO_HISTORY_FILE_TOPIC){
            handVideoHistoryFile(req_json);
        }else {
            throw std::runtime_error("Unsupported topic: " + topic);
        }
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Invalid JSON payload: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to handle message: " + std::string(e.what()));
    }
}

// 占位实现（需根据业务逻辑补充）
void HTTPCommandController::handleGetRealImage(const nlohmann::json& j) {
    // 示例：调用调度器执行获取实时图像任务
    // scheduler_.scheduleJob("get_real_image", j);
    nlohmann::json resp;
    std::string camId = j.value("cameraId",j.value("deviceId", ""));
    std::string nvrId = j.value("nvrId",j.value("nvrId", ""));
    auto task = std::make_shared<GetCameraRealImageTask>(camId,nvrId);
    scheduler_.submit(task, "http");
}

void HTTPCommandController::handleGetSensorData(const nlohmann::json& j) {
    // 获取传感器数据逻辑
}

void HTTPCommandController::handleGetAllDeviceStatus(const nlohmann::json& j) {
    // 获取所有设备状态逻辑
    nlohmann::json resp;
    auto task = std::make_shared<GetDeviceStatusTask>();
    scheduler_.submit(task, "http");
}

void HTTPCommandController::handVideoHistory(const nlohmann::json& j){
    if(!j.contains("deviceId") || !j.contains("nvrId")) return;
    std::string deviceId = j["deviceId"];//2
    std::string nvrId = j["nvrId"];//nvr_001
    std::string startTime = j["startTime"];//那一日的开始//  yyyy-MM-dd HH:mm:ss
    std::string endTime = j["endTime"];//那一日的结束
    auto task = std::make_shared<GetVideoHistoryTask>(deviceId,nvrId,startTime,endTime);
    int id = scheduler_.submit(task, "http");
    std::cout << "handVideoHistory id=" << id 
              << " for device=" << deviceId << " nvrId=" << nvrId << std::endl;
}


void HTTPCommandController::handVideoHistoryFile(const nlohmann::json& j){
    //这里可能存在bug
    if (!j.contains("deviceId") ||!j.contains("nvrId") ||!j.contains("fileName") ||!j.contains("startTime") ||!j.contains("fileSize")) {
        return;
    }
    std::string deviceId = j["deviceId"];
    std::string nvrId = j["nvrId"];
    std::string fileName = j["fileName"];
    std::string startTime = j["startTime"];//开始时间点
    std::string fileSize = j["fileSize"];

    bool pd = FileUtils::keepLatestFiles("/home/ztl/workspace/SmartPatrol-nvr/video_file",5);
    if(!pd){
        std::cout<<"清理视频文件出现问题"<<std::endl;
    }
    DownloadVideoFile in = DownloadVideoFile(deviceId,nvrId,fileName,startTime,fileSize);
    std::cout<<"in fileName "<< in.fileName() << std::endl;
    scheduler_.downloadAndUploadVideo(in);
}


