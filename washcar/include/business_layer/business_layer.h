#pragma once
#include <memory>

class HallService;

class LayerBufferQueue;

class BusinessLayer
{

public:

    explicit BusinessLayer(LayerBufferQueue& bufferQueue);

    ~BusinessLayer();

    bool initialize();

    HallService& hallService();


private:

    LayerBufferQueue& bufferQueue_;
    
    std::unique_ptr<HallService> hallService_;

};