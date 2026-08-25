#ifndef AI_MODEL_SERVICE_H
#define AI_MODEL_SERVICE_H
#include <mutex>
#include <string>
#include "iai_model_service.h"
extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}


class AIModelService : public IAIModelService{
public:
    explicit AIModelService(const std::string& model_path);
    ~AIModelService();
    
    int infer(image_buffer_t* rgb_image,object_detect_result_list* od_results) override;

    int getModelInputWidth() override;
    int getModelInputHeight() override;
private:
    bool initialize();
    bool initModel();

private:
    std::string modelPath_;
    bool initialized_ = false;
    bool postprocess_inited_ = false;
    rknn_app_context_t rknn_app_ctx_;
    const int model_input_width_ = 640;
    const int model_input_height_ = 640;
};

#endif // AI_MODEL_SERVICE_H
