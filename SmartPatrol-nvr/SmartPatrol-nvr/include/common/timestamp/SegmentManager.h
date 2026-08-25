// SegmentManager.h
#ifndef SEGMENT_MANAGER_H
#define SEGMENT_MANAGER_H
#include "video_storage.h"
#include <chrono>
#include <memory>
#include <string>

class SegmentManager {
public:
    SegmentManager(const std::string& dir, const std::string& format, int durationSec);

    bool needNewSegment();
    void cleanExpiredFiles(int retainDays);
    std::unique_ptr<VideoStorage> createSegment(AVFormatContext* inputCtx);

private:
    std::string dir_;
    std::string format_;
    int durationSec_;
    std::chrono::steady_clock::time_point lastStart_;
    int segmentIndex_;
};
#endif