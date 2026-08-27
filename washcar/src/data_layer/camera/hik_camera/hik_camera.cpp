#include "data_layer/camera/hik_camera/hik_camera.h"

bool HikCamera::initialize()
{

    connected_ = true;

    return true;

}


bool HikCamera::start()
{

    if(!connected_)
    {
        return false;
    }

    return true;

}


void HikCamera::stop()
{



}
