#ifndef IAI_DETECTOR_H
#define IAI_DETECTOR_H

#include "ivideo.h"
#include <vector>
#include <string>

struct DetectionResult {
    std::string label;
    float confidence;
    int x, y, width, height;
    long timestamp;
    
    DetectionResult() : confidence(0.0f), x(0), y(0), width(0), height(0), timestamp(0) {}
};

class IAIDetector {
public:
    virtual ~IAIDetector() = default;
    virtual bool initialize() = 0;
    virtual bool loadModel(const std::string& model_path) = 0;
    virtual std::vector<DetectionResult> detect(const VideoFrame& frame) = 0;
    virtual std::vector<DetectionResult> detect(const std::vector<uint8_t>& image_data) = 0;
    virtual void setConfidenceThreshold(float threshold) = 0;
    virtual float getConfidenceThreshold() const = 0;
    virtual bool isModelLoaded() const = 0;
};

#endif