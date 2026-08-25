#include "ai_model_service.h"
#include "device_manager.h"
#include "ai_recognize.h"

const std::string MODELPATH = "/home/ztl/workspace/SmartPatrol-nvr/lib/model/yolov8n_3568_i8.rknn";
const std::string CONFIGPATH = "/home/ztl/workspace/SmartPatrol-nvr/include/common/config/config.json";
int main(){

    ConfigParser::getInstance().loadFromFile(CONFIGPATH);
    std::shared_ptr<IDeviceManager> ideviceManager = std::make_shared<DeviceManager>();

    auto model = std::make_unique<AIModelService>(MODELPATH);
    AIRecognizer ai(std::move(model),ideviceManager.get());
    ai.start();
    
    std::cout << "System running..." << std::endl;
    while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
}