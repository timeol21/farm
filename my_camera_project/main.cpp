#include "CameraHandler.h"
#include <iostream>
#include <vector>
#include <unistd.h>

int main() {
    // 创建摄像头处理对象列表
    // 参数：RTSP地址，图片保存路径，抓拍间隔（秒）
    CameraHandler cam1("rtsp://admin:Wlkjaqxy411@10.9.255.21:554/Streaming/Channels/201", "cam1_latest.jpg", 5);
    CameraHandler cam2("rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101", "cam2_latest.jpg", 5);

    std::cout << "[系统] 正在启动多路监控..." << std::endl;
    
    if(cam1.start()) std::cout << "[OK] 摄像头 1 已启动" << std::endl;
    if(cam2.start()) std::cout << "[OK] 摄像头 2 已启动" << std::endl;

    // 主线程阻塞
    while (true) {
        sleep(1);
    }

    return 0;
}

/*
g++ main.cpp CameraHandler.cpp -o camera_app -lavformat -lavcodec -lavutil -lswscale -lpthread
*/