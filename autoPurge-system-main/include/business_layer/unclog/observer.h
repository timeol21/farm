#pragma once

#include "business_layer/detection/detection_service_object.h"
class Observer{
public:
    virtual ~Observer() = default;

    virtual void onBlockDetected(const DetectionResult& result) = 0;
};

