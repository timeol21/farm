#include "common/log/log_manager.h"
#include "data_layer/camera/camera.h"
#include "data_layer/camera/camera_manager.h"


CameraManager::CameraManager()
{

}

CameraManager::~CameraManager()
{
    stop();
}

bool CameraManager::initialize()
{
    

    for(auto& camera : cameras_)
    {
        
        if(camera == nullptr)
        {
            Logger::error("[System] initialize failed");
            continue;
        }


        if(!camera->initialize())
        {
            Logger::error("[System] camera initialize failed");
            return false;
        }else{
            Logger::info("[System] camera initialize successful");
        }

    }

    return true;

}

void CameraManager::addCamera(
    std::unique_ptr<Camera> camera
)
{

    if(!camera)
    {
        Logger::error(
            "[CameraManager] add null camera"
        );

        return;
    }


    cameras_.push_back(
        std::move(camera)
    );

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
