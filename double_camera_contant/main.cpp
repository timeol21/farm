#include "CameraHandler.h"     //可以稳定的输出两路的视频流和对应的维护照片
#include <iostream>
#include <vector>
#include <unistd.h>
#include <csignal>

// 全局指针，用于安全退出
CameraHandler* g_cam1 = nullptr;
CameraHandler* g_cam2 = nullptr;

// 处理 Ctrl+C 信号，确保程序退出时安全关闭文件和线程
void signalHandler(int signum) {
    std::cout << "\n[系统] 接收到退出信号 (" << signum << ")，正在关闭摄像头..." << std::endl;
    if (g_cam1) g_cam1->stop();
    if (g_cam2) g_cam2->stop();
    exit(signum);
}

int main() {
    // 注册信号捕获
    signal(SIGINT, signalHandler);

    // ================= 摄像头配置 =================
    // 参数说明：RTSP地址, 命名前缀, 抓拍间隔(秒)
    std::string url1 = "rtsp://admin:Wlkjaqxy411@10.9.255.21:554/Streaming/Channels/201";
    std::string url2 = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";

    // 创建处理器对象
    CameraHandler cam1(url1, "cam1", 5);
    CameraHandler cam2(url2, "cam2", 5);

    g_cam1 = &cam1;
    g_cam2 = &cam2;

    std::cout << "========================================" << std::endl;
    std::cout << "   多路监控系统启动 (RK356X)" << std::endl;
    std::cout << "   功能: 60秒视频切片 + 5秒定时抓拍" << std::endl;
    std::cout << "========================================" << std::endl;

    // 启动处理逻辑 (每路会自动开启录制和抓拍两个线程)
    cam1.start();
    cam2.start();

    std::cout << "[OK] 所有任务已进入后台运行。" << std::endl;
    std::cout << "[提示] 按 Ctrl+C 停止程序并保存文件。" << std::endl;

    // 主线程进入循环，维持程序运行
    while (true) {
        // 您可以在这里添加系统状态监控，比如打印 CPU 占用或磁盘剩余空间
        sleep(10);
    }

    return 0;
}
/*
g++ main.cpp CameraHandler.cpp -o camera_system -lavformat -lavcodec -lavutil -lswscale -lpthread
./camera_system
*/