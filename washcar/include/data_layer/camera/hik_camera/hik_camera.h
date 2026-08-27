#pragma once

#include "data_layer/camera/camera.h"

class HikCamera : public Camera
{

public:

    HikCamera() = default;

    ~HikCamera() override = default;

    bool initialize() override;

    bool start() override;

    void stop() override;


private:

    bool connected_ = false;
    // 海康自己的变量

};