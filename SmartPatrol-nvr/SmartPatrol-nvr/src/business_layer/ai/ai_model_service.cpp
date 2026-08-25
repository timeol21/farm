#include "ai_model_service.h"
#include "file_utils.h"
#include <iostream>
#include "ImageProcessor.h"
#include <unistd.h> 
#include <sys/stat.h>
AIModelService::AIModelService(const std::string& model_path)
    : modelPath_(model_path) {
        memset(&rknn_app_ctx_, 0, sizeof(rknn_app_context_t));
        initialize();
    }

AIModelService::~AIModelService() {
    if (initialized_) {
        release_yolov8_model(&rknn_app_ctx_);
    }
    if (postprocess_inited_) {
        deinit_post_process();
    }
}

bool AIModelService::initialize() {
    if (init_post_process() != 0) {
        std::cerr << "init_post_process failed!" << std::endl;
        return false;
    }
    postprocess_inited_ = true;
    if(!initModel()){
        std::cerr<<"Failed to initialize YOLOV8 model!"<<std::endl;
        return false;
    }
    std::cout<<"Model loaded from "<<modelPath_<<std::endl;
    initialized_ = true;
    return true;
}

bool AIModelService::initModel(){
    // 前置检查1：文件是否存在
    if (access(modelPath_.c_str(), F_OK) != 0) {
        std::cerr << "Error: RKNN model file not exist - " << modelPath_ << std::endl;
        return false;
    }
    // 前置检查2：文件是否可读
    if (access(modelPath_.c_str(), R_OK) != 0) {
        std::cerr << "Error: No read permission for RKNN file - " << modelPath_ << std::endl;
        return false;
    }


    int ret = init_yolov8_model(modelPath_.c_str(), &rknn_app_ctx_);
    if( ret != 0){
        std::cerr<<"Failed to initialize YOLOV8 model from "<<modelPath_<<std::endl;
        return false;
    }
    return true;
}
int AIModelService::infer(image_buffer_t* rgb_image,object_detect_result_list* od_results) {
    if (!initialized_) return false;
    int ret = inference_yolov8_model(&rknn_app_ctx_, rgb_image, od_results);
    if(ret!=0){
        std::cerr<<"inference_yolov8_model failed! ret="<<ret<<std::endl;
        return -1;
    }
    return 0;
}

int AIModelService::getModelInputWidth(){
    return this->model_input_width_;
}
int AIModelService::getModelInputHeight(){
    return this->model_input_height_;
}