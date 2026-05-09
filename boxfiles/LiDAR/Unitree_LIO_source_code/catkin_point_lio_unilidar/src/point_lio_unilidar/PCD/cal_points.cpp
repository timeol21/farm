#include <iostream>   //用来过滤和保存符合阈值的数据
#include <vector>
#include <algorithm>
#include <cstdio>


// g++ -O3 filter.cpp -o filter

/**
 * 针对 5,000,000+ 点云设计的超轻量级过滤程序
 * 内存占用：~20MB (500万点)
 * 时间复杂度：O(n)
 */
int main() {
    const char* input_path = "final_raw_points.txt";
    const char* output_path = "output.txt";
    const float discard_ratio = 0.2f; // 丢弃最低的 20%，保留 80%

    // --- 第一遍扫描：只获取强度值，确定阈值 ---
    std::vector<float> intensities;
    intensities.reserve(5000000); // 预分配内存，防止多次扩容

    FILE* fp = fopen(input_path, "r");
    if (!fp) {
        perror("无法打开输入文件");
        return 1;
    }

    float x, y, z, intensity, r5, r6;
    // 只提取强度列，其他数据暂时丢弃
    while (fscanf(fp, "%f, %f, %f, %f, %f, %f", &x, &y, &z, &intensity, &r5, &r6) == 6) {
        intensities.push_back(intensity);
    }

    if (intensities.empty()) {
        fclose(fp);
        return 0;
    }

    // 使用 nth_element 快速定位阈值（非全排序，效率极高）
    size_t target_idx = static_cast<size_t>(intensities.size() * discard_ratio);
    std::nth_element(intensities.begin(), intensities.begin() + target_idx, intensities.end());
    float threshold = intensities[target_idx];

    printf("统计: 总点数 = %zu, 强度阈值 = %.2f\n", intensities.size(), threshold);

    // 释放强度数组内存，为第二步腾出空间（虽然只有20MB，但为了极致少用内存）
    std::vector<float>().swap(intensities);

    // --- 第二遍扫描：流式过滤输出 ---
    rewind(fp); // 文件指针回到开头
    FILE* out_fp = fopen(output_path, "w");
    if (!out_fp) {
        fclose(fp);
        return 1;
    }

    int saved_count = 0;
    while (fscanf(fp, "%f, %f, %f, %f, %f, %f", &x, &y, &z, &intensity, &r5, &r6) == 6) {
        if (intensity >= threshold) {
            // 满足条件则直接写入，不存内存
            fprintf(out_fp, "%.6f, %.6f, %.6f, %.2f, %.6f, %.0f\n", x, y, z, intensity, r5, r6);
            saved_count++;
        }
    }

    fclose(fp);
    fclose(out_fp);

    printf("处理完成。保留点数: %d, 结果已写入 %s\n", saved_count, output_path);
    return 0;
}


// #include <iostream>
// #include <fstream>
// #include <vector>
// #include <string>
// #include <sstream>
// #include <algorithm>

// struct Point {
//     double x, y, z, intensity, r5, r6;
// };

// /**
//  * 自动计算并筛选反射强度的程序
//  * @param discard_ratio 丢弃掉低反射率点的比例。例如 0.2 表示丢弃最低的 20%，保留 80%
//  */
// void filterPoints(const std::string& input_file, const std::string& output_file, double discard_ratio) {
//     std::ifstream infile(input_file);
//     std::vector<Point> all_points;
//     std::vector<double> intensities;
//     std::string line;

//     if (!infile.is_open()) {
//         std::cerr << "无法打开输入文件！" << std::endl;
//         return;
//     }

//     // 1. 读取数据
//     while (std::getline(infile, line)) {
//         if (line.empty()) continue;
//         std::replace(line.begin(), line.end(), ',', ' '); // 处理逗号分隔
//         std::stringstream ss(line);
//         Point p;
//         if (ss >> p.x >> p.y >> p.z >> p.intensity >> p.r5 >> p.r6) {
//             all_points.push_back(p);
//             intensities.push_back(p.intensity);
//         }
//     }
//     infile.close();

//     if (intensities.empty()) return;

//     // 2. 计算阈值
//     // 排序以找到分位数
//     std::vector<double> sorted_intensities = intensities;
//     std::sort(sorted_intensities.begin(), sorted_intensities.end());
    
//     int threshold_idx = static_cast<int>(sorted_intensities.size() * discard_ratio);
//     double threshold = sorted_intensities[threshold_idx];

//     std::cout << "--- 统计信息 ---" << std::endl;
//     std::cout << "总点数: " << all_points.size() << std::endl;
//     std::cout << "过滤比例 (丢弃最低): " << discard_ratio * 100 << "%" << std::endl;
//     std::cout << "计算得到的强度阈值: " << threshold << std::endl;

//     // 3. 写入过滤后的文件
//     std::ofstream outfile(output_file);
//     int saved_count = 0;
//     for (const auto& p : all_points) {
//         if (p.intensity >= threshold) {
//             outfile << p.x << ", " << p.y << ", " << p.z << ", " 
//                     << p.intensity << ", " << p.r5 << ", " << p.r6 << "\n";
//             saved_count++;
//         }
//     }
//     outfile.close();

//     std::cout << "筛选完成，保留点数: " << saved_count << "，已保存至: " << output_file << std::endl;
// }

// int main() {
//     // 设置你想要的参数
//     std::string input = "input.txt";   // 你的原始点云文件
//     std::string output = "output.txt"; // 过滤后的文件
    
//     // 如果你想保留 80% 的高强度点，则丢弃最低的 20%
//     double discard_ratio = 0.20; 

//     filterPoints(input, output, discard_ratio);

//     return 0;
// }