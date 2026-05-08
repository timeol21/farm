#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cmath>

struct Point {
    double x, y, z;
};

struct Voxel {
    std::vector<Point> points;
    double min_x = 1e9, max_x = -1e9;
    double min_y = 1e9, max_y = -1e9;
    double min_z = 1e9, max_z = -1e9;

    void addPoint(const Point& p) {
        points.push_back(p);
        if (p.x < min_x) min_x = p.x; if (p.x > max_x) max_x = p.x;
        if (p.y < min_y) min_y = p.y; if (p.y > max_y) max_y = p.y;
        if (p.z < min_z) min_z = p.z; if (p.z > max_z) max_z = p.z;
    }
};

int main() {
    // ====================== 配置区域 ======================
    const std::string dir = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/";
    const std::string input_file = "final_raw_points.txt";
    const std::string report_file = "lidar_precision_report.txt";

    // 搜索格子的步长（决定了分析的精细程度）
    const double VX = 0.4;  
    const double VY = 0.4;
    const double VZ = 0.4; 

    // 平面判定阈值：如果某方向厚度小于此值，视为平面候选
    const double THICKNESS_THRESHOLD = 0.05; // 5厘米以内视为“薄面”
    const int MIN_POINTS = 80;               // 至少80个点才分析
    // =====================================================

    std::ifstream infile(dir + input_file);
    std::ofstream report(dir + report_file);

    if (!infile.is_open() || !report.is_open()) {
        std::cerr << "文件打开失败！" << std::endl;
        return -1;
    }

    std::map<std::string, Voxel> grid;
    std::string line;

    std::cout << "正在读取数据并生成深度报告..." << std::endl;

    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        for (auto &ch : line) if (ch == ',') ch = ' ';
        std::stringstream ss(line);
        Point p;
        if (ss >> p.x >> p.y >> p.z) {
            int ix = static_cast<int>(std::floor(p.x / VX));
            int iy = static_cast<int>(std::floor(p.y / VY));
            int iz = static_cast<int>(std::floor(p.z / VZ));
            std::string key = std::to_string(ix) + "," + std::to_string(iy) + "," + std::to_string(iz);
            grid[key].addPoint(p);
        }
    }

    // 写入报告头部
    report << "==========================================================\n";
    report << "            雷达点云精度与平面特征深度分析报告\n";
    report << "==========================================================\n\n";
    report << "分析参数设定:\n";
    report << "- 局部搜索步长: X=" << VX << "m, Y=" << VY << "m, Z=" << VZ << "m\n";
    report << "- 平面厚度阈值: < " << THICKNESS_THRESHOLD << "m\n";
    report << "- 最小有效点数: " << MIN_POINTS << " points\n\n";

    int plane_count = 0;
    for (auto const& [key, v] : grid) {
        if (v.points.size() < MIN_POINTS) continue;

        double dx = v.max_x - v.min_x;
        double dy = v.max_y - v.min_y;
        double dz = v.max_z - v.min_z;
        double thickness = std::min({dx, dy, dz});

        if (thickness <= THICKNESS_THRESHOLD) {
            plane_count++;
            report << "【发现疑似平面区域 #" << plane_count << "】\n";
            report << "  - 空间范围定位:\n";
            report << "    X轴: [" << v.min_x << " 至 " << v.max_x << "] 跨度: " << dx << "m\n";
            report << "    Y轴: [" << v.min_y << " 至 " << v.max_y << "] 跨度: " << dy << "m\n";
            report << "    Z轴: [" << v.min_z << " 至 " << v.max_z << "] 跨度: " << dz << "m\n";
            
            // 特点描述
            report << "  - 几何特点描述: ";
            if (thickness == dz) report << "该区域表现为一个[水平面]特征。";
            else if (thickness == dx) report << "该区域表现为一个[垂直于X轴的立面]特征。";
            else report << "该区域表现为一个[垂直于Y轴的立面]特征。";
            
            report << "\n  - 精度评估: 当前区域点云厚度(误差)约为 " << thickness * 1000 << " mm。\n";
            report << "  - 数据密度: 共有 " << v.points.size() << " 个采样点在此小范围内聚集。\n";
            report << "----------------------------------------------------------\n\n";
        }
    }

    report << "总结: 在当前点云数据中，共识别出 " << plane_count << " 处高精度平面特征区域。\n";
    report.close();

    std::cout << "报告生成成功！请查看: " << dir + report_file << std::endl;

    return 0;
}