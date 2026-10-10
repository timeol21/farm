#ifndef YOLO_DETECTOR_H
#define YOLO_DETECTOR_H


class YoloDetector
{

public:

    bool run();


private:

    const char* yolo_path =
    "/home/ztl/program/Check/Check_demo/"
    "rknn_zoo_base/install/"
    "rk356x_linux_aarch64/"
    "rknn_yolov5_demo";

};


#endif