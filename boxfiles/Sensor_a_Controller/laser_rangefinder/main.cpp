#include "laser_host.h"

int main() {
    LaserHost host("/dev/ttyACM0");
    host.init(); 

    uint8_t buffer[512];
    int distance;
    // 地址 0x01 的模块，测量结果 +50mm
    host.setMeasureOffset(0x01, 50);

    // 地址 0x02 的模块，测量结果 -30mm
    host.setMeasureOffset(0x02, -30);
    // 举例：控制地址 1
    host.singleMeasureFast(0x01);
    int len = host.readResponse(buffer, 4000);
    if (len >0) host.parseDistance(buffer, len, distance);

    // 一键让所有激光从机同时测量
    host.broadcastAllMeasure();

    // 连续测量
    host.continuousMeasureAuto(0x01);

    // 按 任意键（Enter） 停止
    getchar(); 
    host.stopContinuousMeasure();
    //开启关闭指定从机地址的激光,传入一个动态数组，可以实现一键全部打开激光
    host.allLaserOn({0x01, 0x02, 0x03});
    host.allLaserOff({0x01, 0x02, 0x03});

    host.deinit();
    return 0;
}
