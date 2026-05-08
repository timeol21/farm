#pragma once

#include "common/application/lifecycle.h"
#include "business_layer/unclog/observer.h"
#include <functional>

class IDetectionService{
public:
    virtual ~IDetectionService() = default;

    virtual void registerObserver(Observer* observer) = 0;
    
};


class DetectionService : public IDetectionService , public ILifecycle{
public:

    DetectionService();
    ~DetectionService();

    void registerObserver(Observer* observer) override;

    void start() override;

    void stop() override;

    std::function<void(const DetectionResult&)> onBlockDetected;
private:
    Observer* unclog;


    //ai 

    //雷达
};