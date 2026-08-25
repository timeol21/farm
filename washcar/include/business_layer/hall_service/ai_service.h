#pragma once
#include <queue>
#include <mutex>

struct ImageFrame
{
    void* data;
    int width;
    int height;

};

struct AIResult
{
    int type;
    float confidence;

};

class AIService
{
public:
    bool initialize();

    bool pushFrame(const ImageFrame& frame);

    bool popResult(AIResult& result);

private:
    void inference();

    std::queue<ImageFrame> frameBuffer_;

    std::queue<AIResult> resultBuffer_;

    std::mutex frameMutex_;

    std::mutex resultMutex_;

};