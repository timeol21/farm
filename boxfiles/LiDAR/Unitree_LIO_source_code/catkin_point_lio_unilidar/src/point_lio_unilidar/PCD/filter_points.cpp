#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>

int main() {
    // ====================== 配置区域：只改这里 ======================
    
    // 1. 文件存放的目录路径 (末尾请记得加斜杠 /)
    const std::string dir = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/";
    
    // 2. 待处理的原始文件名
    const std::string input_file  = "final_raw_points.txt";
    
    // 3. 过滤后保存的新文件名
    const std::string output_file = "filtered_200_points.txt";
    
    // 4. 反射强度过滤阈值 (低于此值的点会被剔除)
    const float THRESHOLD = 200.0f;

    // ===============================================================

    // 组合成完整路径
    const std::string full_input_path  = dir + input_file;
    const std::string full_output_path = dir + output_file;

    std::ifstream infile(full_input_path);
    if (!infile.is_open()) {
        std::cerr << "【错误】无法打开原始文件，请检查目录和文件名: \n" << full_input_path << std::endl;
        return -1;
    }

    std::ofstream outfile(full_output_path);
    if (!outfile.is_open()) {
        std::cerr << "【错误】无法创建保存文件，请检查目录权限: \n" << full_output_path << std::endl;
        return -1;
    }

    std::string line;
    long total_points = 0;
    long saved_points = 0;

    std::cout << ">>> 开始处理..." << std::endl;
    std::cout << ">>> 正在读取: " << input_file << std::endl;

    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        total_points++;

        // 临时处理每一行以解析强度
        std::string temp_line = line;
        for (auto &ch : temp_line) if (ch == ',') ch = ' ';

        std::stringstream ss(temp_line);
        std::string part;
        std::vector<std::string> cols;
        while (ss >> part) {
            cols.push_back(part);
        }

        // 第四列是反射强度
        if (cols.size() >= 4) {
            try {
                float intensity = std::stof(cols[3]);
                if (intensity >= THRESHOLD) {
                    outfile << line << "\n";
                    saved_points++;
                }
            } catch (...) {
                continue; 
            }
        }
    }

    std::cout << ">>> 处理完成！" << std::endl;
    std::cout << ">>> 原始总点数: " << total_points << std::endl;
    std::cout << ">>> 保留点数:   " << saved_points << " (阈值 >= " << THRESHOLD << ")" << std::endl;
    std::cout << ">>> 剔除点数:   " << total_points - saved_points << std::endl;
    std::cout << ">>> 结果已存入: " << output_file << std::endl;

    infile.close();
    outfile.close();

    return 0;
}