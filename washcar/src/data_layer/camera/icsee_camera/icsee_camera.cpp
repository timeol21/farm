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
    const std::string& rtspUrl,
    const std::string& cameraId,
    int keyFrameIntervalSec
)
:
cameraId_(cameraId),
rtspUrl_(rtspUrl),
keyFrameIntervalSec_(keyFrameIntervalSec)
{

    rtspClient_ = std::make_unique<RtspClient>();

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
        return false;
    }

    if(!rtspClient_->open(rtspUrl_))
    {
        return false;
    }

    frame_ = av_frame_alloc();


    latestKeyFrame_ = av_frame_alloc();

    if(frame_ == nullptr || latestKeyFrame_ == nullptr)
    {
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
        return false;
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


    /*
        初始化decoder

        packet

          |

          v

        decoder

          |

          v

        frame

    */



    /*
        这里需要RtspClient提供codec参数

        AVCodecParameters

        当前先按照H264处理

    */



    const AVCodec* codec =
        avcodec_find_decoder(
            AV_CODEC_ID_H264
        );



    if(codec == nullptr)
    {
        return false;
    }




    codecContext_ =
        avcodec_alloc_context3(
            codec
        );



    if(codecContext_ == nullptr)
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