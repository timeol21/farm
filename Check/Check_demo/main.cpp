//#include "dao_layer/camera/camera.h"
#include "dao_layer/camera/camera_manager.h"
#include "dao_layer/light/light.h"
#include "dao_layer/ai/yolo_detector/yolo_detector.h"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{  

    CameraManager manager;


    manager.initialize(
        "../config/config.json"
    );
    
    
    
    manager.getCamera(0)->CameraDemoApp();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    manager.getCamera(1)->CameraDemoApp();
    
     /*Camera camera;
    
     printf("CameraDemoAPP Start   !!!\n");
     camera.CameraDemoApp();
     printf("CameraDemoAPP Finish  !!!\n");
     
     std::this_thread::sleep_for(std::chrono::milliseconds(300));
     
     YoloDetector yolo;

     if(yolo.run())
     {
         printf("YOLO detect success\n");
     }
     */
     
    
    // GpioLight warningLight(99);
    // gpio 1  33
    // gpio 2  101
    // gpio 3  32
    // gpio 4  100
    // gpio 5  99


    /*if(!warningLight.init())
    {
        return -1;
    }


    if(warningLight.turnOn())
    {
        std::cout<<"Turn on success"<<std::endl;
    }
    else
    {
        std::cout<<"Turn on failed"<<std::endl;
    }
    
    
    sleep(2);
    
    
    if(warningLight.turnOff())
    {
        std::cout<<"Turn off success"<<std::endl;
    }


    // ²éÑ¯×´Ì¬
    if(warningLight.getState())
    {
        std::cout << "Light is ON" << std::endl;
    }
    else
    {
        std::cout << "Light is OFF" << std::endl;
    }*/

    
    return 0;
}