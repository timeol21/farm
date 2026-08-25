#include "ai_recognize.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include "mqtt_topics.h"
#include "ImageProcessor.h"
const int INTERVAL_TIME = 4;
AIRecognizer::AIRecognizer( std::unique_ptr<IAIModelService> model,
                            IDeviceManager* devMgr,ITaskResultPublisher* publisher)
    : modelService_(std::move(model)),devMgr_(devMgr),publisher_(publisher){}

AIRecognizer::~AIRecognizer(){
    stop();
}

void AIRecognizer::run(VideoFrame data){
    processFrame(data.getFrameData().frame.get(),data.getCameraId());
}

void AIRecognizer::processFrame(AVFrame* frame,const std::string& sourceCamera){
    if(frame == nullptr){
        std::cerr<<"Error: Input frame is null!"<<std::endl;
        return;
    }

    image_buffer_t rgb_image;
    int ret = ImageProcessor::avframeToRGB(frame, modelService_->getModelInputWidth(), modelService_->getModelInputHeight(), &rgb_image);

    if(ret!=0){
        std::cerr<<"Error: Failed to convert AVFrame to RGB image!"<<std::endl;
        return;
    }

    object_detect_result_list od_results;
    ret = modelService_->infer(&rgb_image,&od_results);
    //ret = inference_yolov8_model(&modelService_->rknn_app_ctx_, &rgb_image, &od_results);
    if(ret!=0){
        std::cerr<<"infer failed! ret="<<ret<<std::endl;
        return;
    }
    bool hasObject = od_results.count > 0;

    if(hasObject){
        std::vector<unsigned char> outJpeg;
        std::cout << "[AI] Detected object(s) on camera: " << sourceCamera 
                  << ", count=" << od_results.count << std::endl;

        // 4. 在 RGB 图上绘制框
        ImageProcessor::drawDetections(&rgb_image, od_results);

        ImageProcessor::compressToJpeg(&rgb_image,outJpeg);

        std::string imageBase64 = ImageProcessor::jpegToBase64(outJpeg);
        nlohmann::json j;
        j["cameraId"] =  sourceCamera;
        j["image"] = imageBase64;

        // 5. 调用上传（保存图像 + 上传 MQTT/HTTP）
        if(publisher_){
            publisher_->publish(RESULT_AI_ALARM_TOPIC,j.dump());
        }
    }
    free(rgb_image.virt_addr);
}

//自己管理线程
void AIRecognizer::start(){
    running_ = true;
    worker_ = std::thread(&AIRecognizer::consumeLoop, this);
}


void AIRecognizer::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void AIRecognizer::consumeLoop() {

    while (running_) {
        auto videoFrames = devMgr_->getAllRealImage();
        if(videoFrames.getSuccess())
        {
            for(auto& realImage : videoFrames.getFrames())
            {
                if(realImage.isIntegrity())
                {
                    run(realImage);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(INTERVAL_TIME));
    }
}
