#pragma once


#include <string>


extern "C"
{

#include <libavformat/avformat.h>

}



class RtspMp4Recorder
{

public:

    RtspMp4Recorder(
        const std::string& outputPrefix,
        int segmentSeconds = 60
    );


    ~RtspMp4Recorder();



    bool initialize(
        AVCodecParameters* codecParameters,
        AVRational timeBase
    );


    bool start();


    void stop();



    bool writePacket(
        AVPacket* packet
    );



private:


    bool openOutput();

    void closeOutput();



private:


    std::string outputPrefix_;


    int segmentSeconds_ = 60;



    AVFormatContext* outputContext_ = nullptr;


    AVStream* outputStream_ = nullptr;



    AVCodecParameters* codecParameters_ = nullptr;


    AVRational inputTimeBase_;



    int segmentIndex_ = 0;



    int64_t startPts_ = AV_NOPTS_VALUE;


    bool running_ = false;


};