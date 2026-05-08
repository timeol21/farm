#pragma once


class IVideoDao{
public:
    ~IVideoDao() = default;

};


class VideoDao : public IVideoDao{
public:
    VideoDao()= default;
    
    ~VideoDao()= default;

};