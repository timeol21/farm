#ifndef I_AI_MODEL_SERVICE_H
#define I_AI_MODEL_SERVICE_H
#include "yolov8.h"
#include "image_utils.h"

class IAIModelService
{
public:
    virtual ~IAIModelService() = default;
    virtual int infer(image_buffer_t* rgb_image,object_detect_result_list* od_results) = 0;
    virtual int getModelInputWidth() = 0;
    virtual int getModelInputHeight() = 0;
};



#endif