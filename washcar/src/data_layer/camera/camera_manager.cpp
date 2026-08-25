#include "data_layer/camera/camera_manager.h"
#include "data_layer/camera/camera.h"

CameraManager::CameraManager()
{

}

CameraManager::~CameraManager()
{

}

bool CameraManager::initialize()
{

    for(auto& camera : cameras_)
    {

        if(camera == nullptr)
        {
            continue;
        }


        if(!camera->initialize())
        {
            return false;
        }

    }

    return true;

}


bool CameraManager::start()
{

    for(auto& camera : cameras_)
    {

        if(camera == nullptr)
        {
            continue;
        }

        if(!camera->start())
        {
            return false;
        }

    }

    return true;

}


bool CameraManager::stop()
{

    for(auto& camera : cameras_)
    {

        if(camera == nullptr)
        {
            continue;
        }

        camera->stop();

    }

    return true;

}


bool CameraManager::capture()
{

    for(auto& camera : cameras_)
    {

        if(camera == nullptr)
        {
            continue;
        }

        if(!camera->capture())
        {
            return false;
        }

    }

    return true;

}