#pragma once

#include "data_layer/camera/camera.h"
#include <vector>
#include <memory>
class Camera;
class CameraManager
{

public:

    CameraManager();

    ~CameraManager();


    bool initialize();
    
    void addCamera(std::unique_ptr<Camera> camera);

    bool start();

    bool stop();

    bool capture();


private:


    CameraManager(const CameraManager&) = delete;

    CameraManager& operator=(const CameraManager&) = delete;

    std::vector<std::unique_ptr<Camera>> cameras_;

};