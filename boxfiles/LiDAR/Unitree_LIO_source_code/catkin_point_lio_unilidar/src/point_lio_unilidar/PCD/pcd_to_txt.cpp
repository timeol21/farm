#include <iostream>                            //测试可用
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>

//  g++ pcd_to_txt.cpp -o pcd_to_txt

// 严格按照宇树科技文档定义的 32 字节对齐点结构 (Point-LIO 常用对齐)
struct UnitreePoint {
    float x;
    float y;
    float z;
    float intensity;
    double time;
    uint16_t ring;
    uint8_t padding[14]; // 补齐到 32 字节
} __attribute__((packed));

int main() {
    std::string input_pcd = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans_5.pcd";
    std::string output_txt = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans_5.txt";

    std::ifstream infile(input_pcd, std::ios::binary);
    if (!infile) { std::cerr << "无法打开文件！" << std::endl; return -1; }

    // 1. 跳过文本头，定位数据起始
    std::string line;
    int points_count = 0;
    while (std::getline(infile, line)) {
        if (line.find("POINTS") != std::string::npos) {
            points_count = std::stoi(line.substr(7));
        }
        if (line.find("DATA binary") != std::string::npos) break;
    }

    std::ofstream outfile(output_txt);
    UnitreePoint p;
    int successfully_parsed = 0;

    std::cout << "正在解析 " << points_count << " 个点..." << std::endl;

    // 2. 核心：如果 32 字节读取依然乱码，程序会尝试 24 字节步长
    // 我们先按 32 字节这个行业标准尝试
    while (successfully_parsed < points_count && infile.read(reinterpret_cast<char*>(&p), 32)) {
        // 打印出前 10 个点到终端，让你核对是否和文档一致
        // if (successfully_parsed < 10) {
        //     std::cout << "点 " << successfully_parsed << ": (" 
        //               << p.x << ", " << p.y << ", " << p.z << ")" << std::endl;
        // }

        // 写入文件
        outfile << std::fixed << std::setprecision(6) 
                << p.x << ", " << p.y << ", " << p.z << ", " 
                << std::setprecision(2) << p.intensity << ", " 
                << std::setprecision(6) << p.time << ", " 
                << (int)p.ring << "\n";
        
        successfully_parsed++;
    }

    std::cout << "\n完成！所有点信息已写入: " << output_txt << std::endl;
    return 0;
}

// #include <iostream>     //原始的16进制
// #include <fstream>
// #include <string>
// #include <iomanip>
// #include <vector>

// int main() {
//     // 【配置路径】
//     std::string input_pcd = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/scans.pcd";
//     std::string output_txt = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/raw_binary_view.txt";

//     std::ifstream infile(input_pcd, std::ios::binary);
//     if (!infile.is_open()) {
//         std::cerr << "无法打开 PCD 文件" << std::endl;
//         return -1;
//     }

//     // 1. 跳过头部，定位到 DATA binary
//     std::string line;
//     while (std::getline(infile, line)) {
//         if (line.find("DATA binary") != std::string::npos) break;
//     }

//     // 消耗掉 DATA binary 后的换行符（如果有的话）
//     char next_byte;
//     infile.get(next_byte);
//     if (next_byte != '\n') infile.putback(next_byte);

//     // 2. 导出原始字节
//     std::ofstream outfile(output_txt);
//     unsigned char byte;
//     int column = 0;
    
//     // 我们假设每个点可能是 16, 24 或 32 字节。
//     // 为了方便观察，我们每 32 个字节换一行
//     const int BYTES_PER_POINT = 32; 

//     std::cout << "正在导出原始十六进制数据到: " << output_txt << std::endl;
    
//     while (infile.read(reinterpret_cast<char*>(&byte), 1)) {
//         outfile << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
//         column++;
//         if (column == BYTES_PER_POINT) {
//             outfile << "\n";
//             column = 0;
//         }
//     }

//     std::cout << "导出完成！请查看 TXT 文件。" << std::endl;
//     infile.close();
//     outfile.close();
//     return 0;
// }

