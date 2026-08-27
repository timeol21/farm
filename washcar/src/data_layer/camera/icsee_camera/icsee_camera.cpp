#include "common/log/log_manager.h"
#include "data_layer/camera/icsee_camera/icsee_camera.h"

#include "userinterface_layer/rtsp/rtsp_client.h"

#include <iostream>
#include <chrono>

extern "C"
{

#include <libavcodec/avcodec.h>

#include <libavutil/time.h>

#include <libswscale/swscale.h>

}

IcSeeCamera::IcSeeCamera(
    const DeviceConfig& config
)
{

    cameraId_ = config.id;


    auto& params = config.parameters;



    std::string protocol =
        params.at("protocol").get<std::string>();


    std::string ip =
        params.at("ip").get<std::string>();


    int port = params.at("port").get<int>();


    std::string username =
        params.at("username").get<std::string>();


    std::string password =
        params.at("password").get<std::string>();


    int channel = params.at("channel").get<int>();

    rtspUrl_ = protocol+"://"+username+":"+password+"@"+ip+":"+std::to_string(port)+"/Streaming/Channels/"+std::to_string(channel);

    keyFrameIntervalSec_ = 5;
    
    recordEnable_ = false;

    recordSegmentTime_ = 60;

    if(params.contains("stream"))
    {

        auto stream =
            params.at("stream");


        if(stream.contains("keyFrameInterval"))
        {
            keyFrameIntervalSec_ =
    stream.at("keyFrameInterval").get<int>();
        }

        if(stream.contains("record"))
        {

            auto record = stream.at("record");

            recordEnable_ = record.at("enable").get<bool>();

            recordPath_ = record.at("path").get<std::string>();

            recordSegmentTime_ = record.at("segmentTime").get<int>();

        }

    }


    rtspClient_ =
        std::make_unique<RtspClient>();



    if(recordEnable_)
    {
    
        recorder_ =
            std::make_unique<RtspMp4Recorder>(
                recordPath_,
                recordSegmentTime_
            );
    
    }

}



IcSeeCamera::~IcSeeCamera()
{

    stop();

    if(latestKeyFrame_)
    {
        av_frame_free(
            &latestKeyFrame_
        );
    }

    if(frame_)
    {
        av_frame_free(
            &frame_
        );
    }

    if(codecContext_)
    {
        avcodec_free_context(
            &codecContext_
        );
    }

}



bool IcSeeCamera::initialize()
{

    if(!rtspClient_)
    {
        return false;
    }

    if(!rtspClient_->initialize())
    {
        Logger::error("[System] rtspClient initialize failed");
        return false;
    }else{
        Logger::info("[System] rtspClient initialize successful");
    }

    if(!rtspClient_->open(rtspUrl_))
    {
        Logger::error("[System] rtspClient open failed");
        return false;
    }else{
        Logger::info("[System] rtspClient open successful");
    }

    frame_ = av_frame_alloc();


    latestKeyFrame_ = av_frame_alloc();

    if(frame_ == nullptr || latestKeyFrame_ == nullptr)
    {
        Logger::error("[System] frame or latestKeyFrame is nullptr");
        return false;
    }
    
    return openStream();

}


bool IcSeeCamera::start()
{

    if(running_)
    {
        return true;
    }


    if(!rtspClient_)
    {
        return false;
    }


    if(!rtspClient_->start())
    {
        Logger::error("[System] rtspClient start failed");
        return false;
    }else{
        Logger::info("[System] rtspClient start successful");
    }
    
    if(recordEnable_)
    {
    
        if(!recorder_)
        {
            return false;
        }
    
    
        if(!recorder_->initialize(
            rtspClient_->getVideoCodecParameters(),
            rtspClient_->getVideoTimeBase()
        ))
        {
            return false;
        }
    
    
        if(!recorder_->start())
        {
            Logger::error("[System] recorder start failed");
            return false;
        }else{
            Logger::info("[System] recorder start successful");
        }
    
    }

    running_ = true;


    captureThread_ = std::thread(&IcSeeCamera::captureLoop,this);


    return true;

}



void IcSeeCamera::stop()
{

    running_ = false;


    if(captureThread_.joinable())
    {
        captureThread_.join();
    }



    if(rtspClient_)
    {
        rtspClient_->stop();
    }


}



bool IcSeeCamera::getLatestKeyFrame(AVFrame*& frame)
{

    std::lock_guard<std::mutex> lock(frameMutex_);


    if(!latestKeyFrame_)
    {
        return false;
    }

    frame = latestKeyFrame_;

    return true;

}



void IcSeeCamera::captureLoop()
{


    AVPacket* packet = nullptr;



    while(running_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if(!rtspClient_)
        {
            continue;
        }



        if(!rtspClient_->getPacket(packet))
        {
            continue;
        }




        if(avcodec_send_packet(
            codecContext_,
            packet
        ) < 0)
        {

            av_packet_free(
                &packet
            );

            continue;

        }




        while(
            avcodec_receive_frame(
                codecContext_,
                frame_
            ) >= 0
        )
        {


            processFrame(frame_);


            av_frame_unref(
                frame_
            );

        }





        av_packet_free(
            &packet
        );


    }


}


bool IcSeeCamera::openStream()
{

     AVCodecParameters* codecParameters =
        rtspClient_->getVideoCodecParameters();


    if(codecParameters == nullptr)
    {
        return false;
    }


    const AVCodec* codec =
        avcodec_find_decoder(
            codecParameters->codec_id
        );


    if(codec == nullptr)
    {
        return false;
    }


    codecContext_ =
        avcodec_alloc_context3(codec);



    if(!codecContext_)
    {
        return false;
    }



    if(avcodec_parameters_to_context(
            codecContext_,
            codecParameters
        ) < 0)
    {
        return false;
    }



    if(avcodec_open2(
            codecContext_,
            codec,
            nullptr
        ) < 0)
    {
        return false;
    }


    return true;

}



void IcSeeCamera::closeStream()
{


    if(codecContext_)
    {

        avcodec_free_context(
            &codecContext_
        );

    }



}



void IcSeeCamera::processFrame(AVFrame* frame)
{

    if(frame->pict_type != AV_PICTURE_TYPE_I)
    {
        return;
    }
    //if(!frame->key_frame)
    //{
    //    return;
    //}

    int64_t now = av_gettime()/1000000;


    if(now - lastSaveTime_ < keyFrameIntervalSec_)
    {

        return;

    }


    lastSaveTime_ = now;

    {

        std::lock_guard<std::mutex> lock(frameMutex_);

        av_frame_unref(latestKeyFrame_);

        av_frame_ref(latestKeyFrame_,frame);

    }


    saveFrameAsJpeg(
        frame,
        "./"
        + cameraId_
        + "_key.jpg"
    );

}




bool IcSeeCamera::saveFrameAsJpeg(AVFrame* frame,const std::string& path)
{


    SwsContext* sws = sws_getContext(

            frame->width,

            frame->height,

            (AVPixelFormat)frame->format,


            frame->width,

            frame->height,

            AV_PIX_FMT_YUVJ420P,


            SWS_BILINEAR,

            nullptr,

            nullptr,

            nullptr

        );


    if(!sws)
    {
        return false;
    }

    const AVCodec* jpegCodec =avcodec_find_encoder(AV_CODEC_ID_MJPEG);


    if(!jpegCodec)
    {
        return false;
    }

    AVCodecContext* jpegCtx = avcodec_alloc_context3(jpegCodec);

    jpegCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;

    jpegCtx->width = frame->width;

    jpegCtx->height = frame->height;


    jpegCtx->time_base ={1, 25};


    if(avcodec_open2(
        jpegCtx,
        jpegCodec,
        nullptr
    ) < 0)
    {
        return false;
    }

    AVFrame* jpegFrame = av_frame_alloc();

    jpegFrame->format = jpegCtx->pix_fmt;

    jpegFrame->width = jpegCtx->width;

    jpegFrame->height = jpegCtx->height;

    av_frame_get_buffer(
        jpegFrame,
        32
    );

    sws_scale(

        sws,


        frame->data,

        frame->linesize,


        0,

        frame->height,


        jpegFrame->data,

        jpegFrame->linesize

    );

    AVPacket* pkt = av_packet_alloc();

    avcodec_send_frame(
        jpegCtx,
        jpegFrame
    );

    avcodec_receive_packet(
        jpegCtx,
        pkt
    );

    FILE* file = fopen(path.c_str(), "wb");

    if(file)
    {

        fwrite(
            pkt->data,
            1,
            pkt->size,
            file
        );


        fclose(file);

    }

    av_packet_free(
        &pkt
    );

    av_frame_free(
        &jpegFrame
    );

    avcodec_free_context(
        &jpegCtx
    );

    sws_freeContext(
        sws
    );

    return true;

}