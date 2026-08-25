#ifndef AI_RECGNIZE_TEST_H
#define AI_RECGNIZE_TEST_H

#include <string>
#include "yolov8.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "iai_model_service.h"
#include "idevice_manager.h"
#include "video_data_object.h"
#include "itask_result_publisher.h"
extern "C" {
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

}


class AIRecognizer {
public:
    AIRecognizer(std::unique_ptr<IAIModelService> model,IDeviceManager* devMgr,ITaskResultPublisher* publisher);
    ~AIRecognizer();

    void run(VideoFrame data);
    void start();
    void stop();

private:
    std::unique_ptr<IAIModelService> modelService_;
    IDeviceManager* devMgr_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    ITaskResultPublisher* publisher_;
    void processFrame(AVFrame* frame,const std::string& sourceCamera);
    void consumeLoop();


};

#endif // AI_RECGNIZE_TEST_H