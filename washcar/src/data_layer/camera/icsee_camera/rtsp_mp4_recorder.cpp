#include "data_layer/camera/icsee_camera/rtsp_mp4_recorder.h"


#include <iostream>
#include <iomanip>
#include <sstream>



RtspMp4Recorder::RtspMp4Recorder(
    const std::string& outputPrefix,
    int segmentSeconds
)
:
outputPrefix_(outputPrefix),
segmentSeconds_(segmentSeconds)
{


}



RtspMp4Recorder::~RtspMp4Recorder()
{

    stop();

}



bool RtspMp4Recorder::initialize(
    AVCodecParameters* codecParameters,
    AVRational timeBase
)
{

    codecParameters_ = codecParameters;

    inputTimeBase_ = timeBase;


    return true;

}



bool RtspMp4Recorder::start()
{

    if(running_)
    {
        return true;
    }


    if(!openOutput())
    {
        return false;
    }


    running_ = true;


    return true;

}




void RtspMp4Recorder::stop()
{

    running_ = false;


    closeOutput();

}




bool RtspMp4Recorder::openOutput()
{

    std::ostringstream oss;


    oss
        << outputPrefix_
        << "_"
        << segmentIndex_++
        << ".mp4";



    std::string filename = oss.str();



    if(avformat_alloc_output_context2(
        &outputContext_,
        nullptr,
        "mp4",
        filename.c_str()
    ) < 0)
    {

        return false;

    }



    outputStream_ =
        avformat_new_stream(
            outputContext_,
            nullptr
        );



    if(!outputStream_)
    {
        return false;
    }



    avcodec_parameters_copy(
        outputStream_->codecpar,
        codecParameters_
    );


    outputStream_->codecpar->codec_tag = 0;



    if(!(outputContext_->oformat->flags & AVFMT_NOFILE))
    {

        if(avio_open(
            &outputContext_->pb,
            filename.c_str(),
            AVIO_FLAG_WRITE
        ) <0)
        {
            return false;
        }

    }



    if(avformat_write_header(
        outputContext_,
        nullptr
    ) <0)
    {

        return false;

    }



    std::cout
        << "record start:"
        << filename
        << std::endl;



    return true;

}



void RtspMp4Recorder::closeOutput()
{


    if(!outputContext_)
    {
        return;
    }



    av_write_trailer(
        outputContext_
    );


    if(!(outputContext_->oformat->flags & AVFMT_NOFILE))
    {

        avio_closep(
            &outputContext_->pb
        );

    }



    avformat_free_context(
        outputContext_
    );


    outputContext_ = nullptr;


}




bool RtspMp4Recorder::writePacket(
    AVPacket* packet
)
{

    if(!running_)
    {
        return false;
    }



    AVPacket outPacket;


    av_init_packet(
        &outPacket
    );


    av_packet_ref(
        &outPacket,
        packet
    );



    outPacket.stream_index =
        outputStream_->index;



    outPacket.pts =
        av_rescale_q(
            packet->pts,
            inputTimeBase_,
            outputStream_->time_base
        );


    outPacket.dts =
        outPacket.pts;



    int ret =
        av_interleaved_write_frame(
            outputContext_,
            &outPacket
        );


    av_packet_unref(
        &outPacket
    );


    return ret >=0;

}