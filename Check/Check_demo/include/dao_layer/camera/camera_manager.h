#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H


#include <vector>
#include <memory>
#include <string>


#include "dao_layer/camera/camera.h"
#include "dao_layer/camera/camera_config_data.h"


class CameraManager
{

public:

    CameraManager();


    /*
        加载json
        创建所有camera对象
    */
    bool initialize(const std::string& configPath);



    /*
        根据id获取摄像头
    */
    Camera* getCamera(int id);



    /*
        获取摄像头数量
    */
    int getCameraCount() const;



private:


    std::vector<std::unique_ptr<Camera>> cameras_;


};


#endif