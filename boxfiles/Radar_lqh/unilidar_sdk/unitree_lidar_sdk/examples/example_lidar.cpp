/**********************************************************************
 Copyright (c) 2020-2023, Unitree Robotics.Co.Ltd. All rights reserved.
***********************************************************************/

#include "unitree_lidar_sdk.h"
#include <fstream>
#include <iostream>

using namespace unitree_lidar_sdk;

int main(){

  // Initialize Lidar Object
  UnitreeLidarReader* lreader = createUnitreeLidarReader();
  int cloud_scan_num = 18;
  std::string port_name = "/dev/ttyUSB0";

  if ( lreader->initialize(cloud_scan_num, port_name) ){
    printf("Unilidar initialization failed! Exit here!\n");
    exit(-1);
  }else{
    printf("Unilidar initialization succeed!\n");
  }

  // Set Lidar Working Mode
  lreader->setLidarWorkingMode(NORMAL);
  sleep(1);

  // 初始化文件导出
  std::ofstream outFile("lidar_cloud.txt");
  int frameCount = 0;
  printf("Starting data collection. Target: 50 frames...\n");

  // Parse PointCloud and IMU data
  MessageType result;
  while (true)
  {
    result = lreader->runParse(); 

    switch (result)
    {
    case POINTCLOUD:
      // 1. 获取点云数据引用
      {
          const auto& cloud = lreader->getCloud();
          
          // 2. 逐点写入文件 (格式: X Y Z Intensity Time Ring)
          for (const auto& p : cloud.points) {
            outFile << p.x << " " << p.y << " " << p.z << " " 
                    << p.intensity << " " << p.time << " " << (int)p.ring << "\n";
          }

          frameCount++;
          printf("Saved frame [%d/50], Cloud Size: %ld\n", frameCount, cloud.points.size());

          // 3. 采集满 50 帧自动停止，防止文件过大
          if (frameCount >= 50) {
            outFile.close();
            printf("\n--- SUCCESS ---\n");
            printf("File saved to: bin/lidar_cloud.txt\n");
            printf("You can now transfer this file to Windows for CloudCompare.\n");
            return 0; 
          }
      }
      break;

    case IMU:
      // IMU 数据我们暂时不写文件，只在屏幕提示
      // printf("IMU parsed\n"); 
      break;

    default:
      break;
    }
  }
  
  return 0;
}
