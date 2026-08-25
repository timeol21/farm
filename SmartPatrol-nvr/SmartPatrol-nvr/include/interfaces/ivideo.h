#ifndef IVIDEO_H
#define IVIDEO_H

#include <vector>
#include <memory>
#include <string>

class VideoFrame {
public:
    std::vector<uint8_t> data;
    int width;
    int height;
    int format;
    long timestamp;
    size_t size;
    
    VideoFrame() : width(0), height(0), format(0), timestamp(0), size(0) {}
};

class IVideoSource {
public:
    virtual ~IVideoSource() = default;
    virtual bool initialize() = 0;
    virtual bool startCapture() = 0;
    virtual bool stopCapture() = 0;
    virtual std::unique_ptr<VideoFrame> getFrame() = 0;
    virtual bool saveVideo(const std::string& filename) = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual int getFps() const = 0;
};

class IVideoProcessor {
public:
    virtual ~IVideoProcessor() = default;
    virtual bool decode(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> encode(const VideoFrame& frame) = 0;
    virtual std::vector<uint8_t> extractFrame(const VideoFrame& frame, int quality = 80) = 0;
    virtual bool resizeFrame(const VideoFrame& src, VideoFrame& dst, int width, int height) = 0;
};

#endif