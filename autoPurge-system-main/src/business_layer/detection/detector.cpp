#include "business_layer/detection/detector.h"


AIDetector::AIDetector(){

}

AIDetector::~AIDetector(){

}

bool AIDetector::initialize(){

}
void AIDetector::start() {

}
void AIDetector::stop() {

}

void AIDetector::setCallback(std::function<void(const DetectionResult&)> cb) {

}

void AIDetector::reloadModel(){

}

void AIDetector::convertLoop(){

}

void AIDetector::inferLoop(){

}








RadarDetector::RadarDetector(){
    
}

RadarDetector::~RadarDetector(){
    
}


bool RadarDetector::initialize(){

};

void RadarDetector::start() {
    
}

void RadarDetector::stop() {
    
}

void RadarDetector::setCallback(std::function<void(const DetectionResult&)> cb) {
    
}

void RadarDetector::scanLoop(){
    
}