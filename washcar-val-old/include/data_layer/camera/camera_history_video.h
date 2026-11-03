#ifndef CAMERA_HISTORY_VIDEO_H
#define CAMERA_HISTORY_VIDEO_H

#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

class CameraHistoryVideo {
public:
    CameraHistoryVideo() = default;
    explicit CameraHistoryVideo(const std::string& outputPath);
    ~CameraHistoryVideo();

    bool startRecording(const std::string& outputPath, AVFormatContext* inputFormatCtx);
    bool stopRecording();
    bool isRecording() const { return isRecording_; }

    bool writePacket(AVPacket* packet);

    std::vector<std::string> getSegments();
    void cleanExpiredSegments(int keepDays);

    const std::string& getCurrentPath() const { return currentPath_; }

private:
    bool initOutputContext(AVFormatContext* inputFormatCtx, const std::string& outputPath);

    std::string currentPath_;
    AVFormatContext* outputContext_;
    AVStream* outputStream_;
    int64_t startTime_;
    int frameCount_;
    bool isRecording_ = false;
};

#endif
