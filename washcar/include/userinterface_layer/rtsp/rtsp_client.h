#pragma once

#include <string>
#include <thread>
#include <mutex>

extern "C"
{

#include <libavformat/avformat.h>

}

class RtspClient
{

public:

    RtspClient();

    ~RtspClient();

    bool initialize();
    
    bool open(const std::string& url);

    bool start();

    void stop();

    bool getPacket(AVPacket*& packet);
    
    AVCodecParameters* getVideoCodecParameters();
    
    AVRational getVideoTimeBase();


private:

    void receiveLoop();

    bool openStream();

    void closeStream();
    
    

    std::string rtspUrl_;

    AVFormatContext* formatContext_ = nullptr;

    int videoStreamIndex_ = -1;

    std::thread receiveThread_;

    bool running_ = false;

    std::mutex packetMutex_;

    AVPacket* latestPacket_ = nullptr;


};