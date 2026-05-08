#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>

// 1. 定义一个与宇树雷达完全兼容的点类型
struct EIGEN_ALIGN16 UnitreePoint {
    PCL_ADD_POINT4D;                  // 包含 x, y, z
    float intensity;
    double time;
    uint16_t ring;
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW   // 确保内存对齐
} ;

// 注册点类型，这能确保 PCL 知道如何保存这个 32 字节的结构
POINT_CLOUD_REGISTER_POINT_STRUCT (UnitreePoint,
    (float, x, x)
    (float, y, y)
    (float, z, z)
    (float, intensity, intensity)
    (double, time, time)
    (uint16_t, ring, ring)
)

int main() {
    // std::string input_txt = "final_raw_points.txt";
    // 关键修改：添加 ../ 表示去运行目录的上一层查找 
    std::string input_txt = "../filtered_200_points.txt";
    std::string output_pcd = "restored_scans_pcl.pcd";

    std::ifstream infile(input_txt);
    if (!infile) return -1;

    pcl::PointCloud<UnitreePoint> cloud;
    std::string line;

    std::cout << "正在解析 TXT..." << std::endl;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        for (auto& ch : line) if (ch == ',') ch = ' ';
        std::stringstream ss(line);

        UnitreePoint p;
        // 关键：严格匹配你 TXT 的列顺序！
        if (ss >> p.x >> p.y >> p.z >> p.intensity >> p.time >> p.ring) {
            cloud.push_back(p);
        }
    }

    // 使用 PCL 官方接口保存二进制 PCD
    pcl::io::savePCDFileBinary(output_pcd, cloud);
    
    std::cout << "转换成功！共写入 " << cloud.size() << " 个点。" << std::endl;
    std::cout << "PCL 已自动处理 32 字节对齐。" << std::endl;

    return 0;
}