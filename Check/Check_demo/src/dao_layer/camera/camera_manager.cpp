#include "dao_layer/camera/camera_manager.h"


#include <fstream>
#include <iostream>


#include "nlohmann/json.hpp"


using json = nlohmann::json;



CameraManager::CameraManager()
{

}



bool CameraManager::initialize(
        const std::string& configPath)
{


    std::ifstream file(configPath);


    if(!file.is_open())
    {
        printf("open camera config failed\n");
        return false;
    }


    json root;

    file >> root;



    auto cameraArray = root["camera"];



    for(auto& item : cameraArray)
    {


        CameraConfig config;



        config.id =
            item["id"].get<int>();


        config.type =
            item["type"].get<std::string>();


        /*
            serial
        */

        config.serial.tty =
            item["serial"]["tty"]
            .get<std::string>()[0];


        config.serial.baudrate =
            item["serial"]["baudrate"];


        config.serial.databit =
            item["serial"]["databit"]
            .get<std::string>()[0];


        config.serial.debug =
            item["serial"]["debug"]
            .get<std::string>()[0];


        config.serial.echo =
            item["serial"]["echo"]
            .get<std::string>()[0];


        config.serial.fctl =
            item["serial"]["fctl"]
            .get<std::string>()[0];


        config.serial.parity =
            item["serial"]["parity"]
            .get<std::string>()[0];


        config.serial.stopbit =
            item["serial"]["stopbit"]
            .get<std::string>()[0];



        /*
            image
        */

        config.image.resolution =
            item["image"]["resolution"];


        config.image.savePath =
            item["image"]["save_path"];



        /*
            osd
        */

        config.osd.enable =
            item["osd"]["enable"];



        /*
            创建Camera对象
        */

        cameras_.push_back(
              std::unique_ptr<Camera>(
                  new Camera(config)
              )
          );



        printf(
            "create camera id=%d\n",
            config.id
        );

    }



    return true;

}



Camera* CameraManager::getCamera(int id)
{


    for(auto& camera : cameras_)
    {


        if(camera->id()==id)
        {
            return camera.get();
        }

    }


    return nullptr;

}



int CameraManager::getCameraCount() const
{

    return cameras_.size();

}