#include "dao_layer/ai/yolo_detector/yolo_detector.h"

#include <cstdlib>
#include <string>
#include <iostream>


bool YoloDetector::run()
{

    std::string command;


    command =
    "cd "
    + std::string(yolo_path)
    + " && ./rknn_yolov5_demo";


    int ret = system(command.c_str());


    if(ret != 0)
    {
        std::cout
        << "YOLO detect failed"
        << std::endl;

        return false;
    }


    return true;
}