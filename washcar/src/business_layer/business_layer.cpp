#include "business_layer/business_layer.h"

#include "business_layer/hall_service/hall_service.h"

#include "common/layer_buffer_queue/layer_buffer_queue.h"

BusinessLayer::BusinessLayer(
    LayerBufferQueue& bufferQueue
)
:
bufferQueue_(bufferQueue)
{

}

BusinessLayer::~BusinessLayer() 
{

}


bool BusinessLayer::initialize()
{

    hallService_ =std::make_unique<HallService>(bufferQueue_);

    if(!hallService_->initialize())
    {
        return false;
    }

    return true;
}

HallService& BusinessLayer::hallService()
{
    return *hallService_;
}