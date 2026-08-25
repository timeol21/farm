#include "config_parser.h"
#include "job_scheduler.h"
#include "mqtt_command_dispatcher.h"
#include "http_service.h"
#include "task_result_publisher.h"
#include "mqtt_topics.h"
#include "mqtt_service.h"
#include "device_status_reporter.h"
#include "ai_model_service.h"
#include "ai_recognize.h"
#include "device_manager.h"
#include "device_status_reporter.h"
const std::string MODELPATH = "/home/ztl/workspace/allin/allin/model/yolov8n3576_i8.rknn";
const std::string CONFIGPATH = "/home/ztl/workspace/SmartPatrol-nvr/include/common/config/config.json";
#include <thread>

int main(){

    av_log_set_level(AV_LOG_QUIET);
    ConfigParser::getInstance().loadFromFile(CONFIGPATH);

    std::shared_ptr<IDeviceManager> ideviceManager = std::make_shared<DeviceManager>();
    
    JobScheduler scheduler(8, ideviceManager.get());
    HttpPublisher httpPublisher("http://127.0.0.1:8080/report"); //客服端
    scheduler.setHttpPublisher(&httpPublisher);
    // start HTTP service   
    HTTPCommandController controller(scheduler);
    WebService webService("127.0.0.1", &controller);
    if (!webService.start()) {
        std::cerr << "WebService 启动失败（端口8080可能被占用）" << std::endl;
        return -1;
    }

    // auto model = std::make_unique<AIModelService>(MODELPATH);
    // AIRecognizer ai(std::move(model),ideviceManager.get());
    // ai.start();  

    // //定时上报设备状态启动 mqtt
    // DeviceStatusReporter reporter(ideviceManager.get(),&mqttPublisher);
    // reporter.startAutoReport(RESULT_GET_ALL_DEVICE_STATUS_TOPIC,15);

    webService.stop();
    std::cout << "System running..." << std::endl;
    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
    std::cout << "所有资源已释放，程序正常退出" << std::endl;
}