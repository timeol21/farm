#include "common/log/log_manager.h"
#include "userinterface_layer/rtsp/rtsp_client.h"

#include <iostream>
#include <chrono>

extern "C"
{
#include <libavformat/avformat.h>
}

RtspClient::RtspClient()
{

}

RtspClient::~RtspClient()
{

    stop();

}

bool RtspClient::initialize()
{

    avformat_network_init();

    return true;

}

bool RtspClient::open(const std::string& url)
{
    rtspUrl_ = url;

    return true;

}


bool RtspClient::start()
{

    if(running_)
    {
        return true;
    }

    if(!openStream())
    {
        return false;
    }

    running_ = true;

    receiveThread_ = std::thread(&RtspClient::receiveLoop,this);

    return true;

}



void RtspClient::stop()
{

    running_ = false;
    
    if(formatContext_)
    {
        avformat_close_input(
            &formatContext_
        );
    }


    if(receiveThread_.joinable())
    {
        receiveThread_.join();
    }



    closeStream();

}



bool RtspClient::getPacket(
    AVPacket*& packet
)
{

    std::lock_guard<std::mutex> lock(
        packetMutex_
    );



    if(latestPacket_ == nullptr)
    {
        return false;
    }



    packet =
        av_packet_alloc();



    av_packet_ref(
        packet,
        latestPacket_
    );



    return true;

}

AVCodecParameters* RtspClient::getVideoCodecParameters()
{

    if(!formatContext_)
    {
        return nullptr;
    }


    if(videoStreamIndex_ < 0)
    {
        return nullptr;
    }


    return formatContext_
        ->streams[videoStreamIndex_]
        ->codecpar;

}

AVRational RtspClient::getVideoTimeBase()
{

    if(!formatContext_)
    {
        return AVRational{0,1};
    }


    if(videoStreamIndex_ < 0)
    {
        return AVRational{0,1};
    }


    return formatContext_
        ->streams[videoStreamIndex_]
        ->time_base;

}


void RtspClient::receiveLoop()
{

    AVPacket* packet =
        av_packet_alloc();



    while(running_)
    {


        if(
            av_read_frame(
                formatContext_,
                packet
            )
            < 0
        )
        {
            continue;

        }

        if(
            packet->stream_index
            ==
            videoStreamIndex_
        )
        {

            std::lock_guard<std::mutex> lock(
                packetMutex_
            );

            if(latestPacket_)
            {

                av_packet_unref(
                    latestPacket_
                );

            }
            else
            {

                latestPacket_ = av_packet_alloc();

            }

            av_packet_ref(
                latestPacket_,
                packet
            );


        }

        av_packet_unref(
            packet
        );


    }

    av_packet_free(
        &packet
    );

}

bool RtspClient::openStream()
{

    AVDictionary* options = nullptr;

    av_dict_set(
        &options,
        "rtsp_transport",
        "tcp",
        0
    );

    if(
        avformat_open_input(
            &formatContext_,
            rtspUrl_.c_str(),
            nullptr,
            &options
        )
        <0
    )
    {

        return false;

    }

    if(
        avformat_find_stream_info(
            formatContext_,
            nullptr
        )
        <0
    )
    {

        return false;

    }

    videoStreamIndex_
        =
        av_find_best_stream(
            formatContext_,
            AVMEDIA_TYPE_VIDEO,
            -1,
            -1,
            nullptr,
            0
        );

    return videoStreamIndex_ >=0;

}

void RtspClient::closeStream()
{

    std::lock_guard<std::mutex> lock(
        packetMutex_
    );


    if(latestPacket_)
    {

        av_packet_free(
            &latestPacket_
        );

    }

    if(formatContext_)
    {

        avformat_close_input(
            &formatContext_
        );

    }

}
