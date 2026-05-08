#include "business_layer/detection/detection_service.h"
#include "common/log/log_manager.h"

DetectionService::DetectionService(){

}


DetectionService::~DetectionService(){

}
void DetectionService::registerObserver(Observer* observer){
    unclog = observer;
}

void DetectionService::start() {
    
}

void DetectionService::stop() {
    
}

