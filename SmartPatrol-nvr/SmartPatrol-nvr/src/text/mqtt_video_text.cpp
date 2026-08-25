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
#include <thread>
const std::string MODELPATH = "/home/ztl/workspace/allin/allin/model/yolov8n3576_i8.rknn";
const std::string CONFIGPATH = "/home/ztl/workspace/SmartPatrol-nvr/include/common/config/config.json";

int main(){
    av_log_set_level(AV_LOG_QUIET);
    ConfigParser::getInstance().loadFromFile(CONFIGPATH);
    std::shared_ptr<IDeviceManager> ideviceManager = std::make_shared<DeviceManager>();
    JobScheduler scheduler(8, ideviceManager.get());
    MqttCommandDispatcher cmdDispatcher(scheduler);  //根据接收的主题来选择调用的处理任务，需要依赖jobscheduler的接口提交任务
    MqttService mqtt("mqtt://192.168.31.249", "edge-box", &cmdDispatcher); //需要依赖cmdDispatcher分发相应任务
    MqttPublisher mqttPublisher(&mqtt);
    scheduler.setMqttPublisher(&mqttPublisher); //依赖publisher的唯一原因是需要将publisher传入Taskcontext供具体task调用


    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
    std::cout << "所有资源已释放，程序正常退出" << std::endl;

}
