#pragma once

class Camera
{

public:

    virtual ~Camera() = default;

    virtual bool initialize() = 0;

    virtual bool start() = 0;

    virtual void stop() = 0;

    virtual bool capture() = 0;

};