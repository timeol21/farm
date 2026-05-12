// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <sstream>
// #include <algorithm>
// #include <cmath>
// #include <omp.h>

// struct BestBox {
//     double min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;
//     double density = 0;
//     int point_count = 0;
// };

// // 保持你的 6 参数约束不变
// const double MIN_L = 10.0, MAX_L = 15.0;
// const double MIN_W = 8.0, MAX_W = 14.0;
// const double MIN_H = 0.1, MAX_H = 0.5;
// const double RES = 0.3;

// int main() {
//     // 1. 读取数据并确定边界
//     std::vector<std::vector<double>> pts;
//     double x_min = 1e9, x_max = -1e9, y_min = 1e9, y_max = -1e9, z_min = 1e9, z_max = -1e9;
    
//     std::ifstream infile("output.txt");
//     std::string line;
//     if (!infile) { std::cerr << "找不到 output.txt" << std::endl; return -1; }

//     while (std::getline(infile, line)) {
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         double x, y, z;
//         if (ss >> x >> y >> z) {
//             pts.push_back({x, y, z});
//             x_min = std::min(x_min, x); x_max = std::max(x_max, x);
//             y_min = std::min(y_min, y); y_max = std::max(y_max, y);
//             z_min = std::min(z_min, z); z_max = std::max(z_max, z);
//         }
//     }

//     // 2. 建立计数网格 (增加缓冲区防止溢出)
//     int NX = std::ceil((x_max - x_min) / RES) + 3;
//     int NY = std::ceil((y_max - y_min) / RES) + 3;
//     int NZ = std::ceil((z_max - z_min) / RES) + 3;
//     std::vector<int> grid(NX * NY * NZ, 0);

//     for (const auto& p : pts) {
//         int ix = (p[0] - x_min) / RES + 1;
//         int iy = (p[1] - y_min) / RES + 1;
//         int iz = (p[2] - z_min) / RES + 1;
//         grid[ix * NY * NZ + iy * NZ + iz]++;
//     }

//     // 3. 计算 3D 积分图 (Summed-area Table)
//     std::vector<int> sum(NX * NY * NZ, 0);
//     for (int i = 1; i < NX; ++i) {
//         for (int j = 1; j < NY; ++j) {
//             for (int k = 1; k < NZ; ++k) {
//                 int val = grid[i * NY * NZ + j * NZ + k];
//                 sum[i * NY * NZ + j * NZ + k] = val 
//                     + sum[(i-1) * NY * NZ + j * NZ + k]
//                     + sum[i * NY * NZ + (j-1) * NZ + k]
//                     + sum[i * NY * NZ + j * NZ + (k-1)]
//                     - sum[(i-1) * NY * NZ + (j-1) * NZ + k]
//                     - sum[(i-1) * NY * NZ + j * NZ + (k-1)]
//                     - sum[i * NY * NZ + (j-1) * NZ + (k-1)]
//                     + sum[(i-1) * NY * NZ + (j-1) * NZ + (k-1)];
//             }
//         }
//     }

//     BestBox final_best;
//     int sl_min = std::round(MIN_L / RES), sl_max = std::round(MAX_L / RES);
//     int sw_min = std::round(MIN_W / RES), sw_max = std::round(MAX_W / RES);
//     int sh_min = std::round(MIN_H / RES), sh_max = std::round(MAX_H / RES);

//     // 4. 并行搜索
//     #pragma omp parallel
//     {
//         BestBox local_best;
//         // 使用 collapse 合并前三层循环，最大化利用多核性能[cite: 1]
//         #pragma omp for collapse(3)
//         for (int i = 1; i < NX - sl_max; ++i) {
//             for (int j = 1; j < NY - sw_max; ++j) {
//                 for (int k = 1; k < NZ - sh_max; ++k) {
//                     for (int sl = sl_min; sl <= sl_max; ++sl) {
//                         for (int sw = sw_min; sw <= sw_max; ++sw) {
//                             for (int sh = sh_min; sh <= sh_max; ++sh) {
//                                 int i2 = i + sl, j2 = j + sw, k2 = k + sh;
                                
//                                 // O(1) 快速体积求和公式[cite: 1]
//                                 int count = sum[i2*NY*NZ + j2*NZ + k2]
//                                           - sum[i*NY*NZ + j2*NZ + k2]
//                                           - sum[i2*NY*NZ + j*NZ + k2]
//                                           - sum[i2*NY*NZ + j2*NZ + k]
//                                           + sum[i*NY*NZ + j*NZ + k2]
//                                           + sum[i*NY*NZ + j2*NZ + k]
//                                           + sum[i2*NY*NZ + j*NZ + k]
//                                           - sum[i*NY*NZ + j*NZ + k];

//                                 double vol = (sl * RES) * (sw * RES) * (sh * RES);
//                                 double d = count / vol;

//                                 if (d > local_best.density) {
//                                     local_best.density = d;
//                                     local_best.point_count = count;
//                                     // 还原物理坐标
//                                     local_best.min_x = x_min + (i) * RES;
//                                     local_best.max_x = x_min + (i2) * RES;
//                                     local_best.min_y = y_min + (j) * RES;
//                                     local_best.max_y = y_min + (j2) * RES;
//                                     local_best.min_z = z_min + (k) * RES;
//                                     local_best.max_z = z_min + (k2) * RES;
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//         // 汇总各线程结果[cite: 1]
//         #pragma omp critical
//         if (local_best.density > final_best.density) {
//             final_best = local_best;
//         }
//     }

//     // 5. 输出结果
//     std::cout << "\n================ 发现最优立体框 (全加速版) ================" << std::endl;
//     std::cout << "找到的最优尺寸 (L x W x H): " 
//               << (final_best.max_x - final_best.min_x) << " x "
//               << (final_best.max_y - final_best.min_y) << " x "
//               << (final_best.max_z - final_best.min_z) << " m" << std::endl;
//     std::cout << "具体物理边界:" << std::endl;
//     std::cout << "  X轴: [" << final_best.min_x << " 至 " << final_best.max_x << "]" << std::endl;
//     std::cout << "  Y轴: [" << final_best.min_y << " 至 " << final_best.max_y << "]" << std::endl;
//     std::cout << "  Z轴: [" << final_best.min_z << " 至 " << final_best.max_z << "]" << std::endl;
//     std::cout << "框内点数: " << final_best.point_count << std::endl;
//     std::cout << "最大占有密度: " << final_best.density << " pts/m³" << std::endl;

//     return 0;
// }

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <sstream>
// #include <algorithm>
// #include <cmath>
// #include <omp.h> // 引入多线程

// struct BestBox {
//     double min_x, max_x, min_y, max_y, min_z, max_z;
//     double density = 0;
//     int point_count = 0;
// };

// // 全局参数（根据你的要求保持不变）
// const double MIN_L = 0.5, MAX_L = 1.0;
// const double MIN_W = 0.5, MAX_W = 1.0;
// const double MIN_H = 0.1, MAX_H = 0.5;
// const double RES = 0.1;

// int main() {
//     // 1. 读取并确定空间边界
//     std::vector<std::vector<double>> pts;
//     double x_min = 1e9, x_max = -1e9, y_min = 1e9, y_max = -1e9, z_min = 1e9, z_max = -1e9;
    
//     std::ifstream infile("output.txt");
//     std::string line;
//     while (std::getline(infile, line)) {
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         double x, y, z;
//         if (ss >> x >> y >> z) {
//             pts.push_back({x, y, z});
//             x_min = std::min(x_min, x); x_max = std::max(x_max, x);
//             y_min = std::min(y_min, y); y_max = std::max(y_max, y);
//             z_min = std::min(z_min, z); z_max = std::max(z_max, z);
//         }
//     }

//     // 2. 建立 3D 计数网格
//     int NX = std::ceil((x_max - x_min) / RES) + 2;
//     int NY = std::ceil((y_max - y_min) / RES) + 2;
//     int NZ = std::ceil((z_max - z_min) / RES) + 2;
//     // 使用一维数组模拟三维，提高缓存命中率
//     std::vector<int> grid(NX * NY * NZ, 0);
//     for (const auto& p : pts) {
//         int ix = (p[0] - x_min) / RES + 1;
//         int iy = (p[1] - y_min) / RES + 1;
//         int iz = (p[2] - z_min) / RES + 1;
//         grid[ix * NY * NZ + iy * NZ + iz]++;
//     }

//     // 3. 核心优化：计算 3D 积分图 (Summed-area Table)
//     std::vector<int> sum(NX * NY * NZ, 0);
//     for (int i = 1; i < NX; ++i) {
//         for (int j = 1; j < NY; ++j) {
//             for (int k = 1; k < NZ; ++k) {
//                 int val = grid[i * NY * NZ + j * NZ + k];
//                 sum[i * NY * NZ + j * NZ + k] = val 
//                     + sum[(i-1) * NY * NZ + j * NZ + k]
//                     + sum[i * NY * NZ + (j-1) * NZ + k]
//                     + sum[i * NY * NZ + j * NZ + (k-1)]
//                     - sum[(i-1) * NY * NZ + (j-1) * NZ + k]
//                     - sum[(i-1) * NY * NZ + j * NZ + (k-1)]
//                     - sum[i * NY * NZ + (j-1) * NZ + (k-1)]
//                     + sum[(i-1) * NY * NZ + (j-1) * NZ + (k-1)];
//             }
//         }
//     }

//     BestBox best;
//     int step_l = MIN_L / RES, max_step_l = MAX_L / RES;
//     int step_w = MIN_W / RES, max_step_w = MAX_W / RES;
//     int step_h = MIN_H / RES, max_step_h = MAX_H / RES;

//     // 4. 并行化搜索
//     #pragma omp parallel
//     {
//         BestBox local_best;
//         #pragma omp for collapse(3) // 将前三层循环合并并行
//         for (int i = 1; i < NX - max_step_l; ++i) {
//             for (int j = 1; j < NY - max_step_w; ++j) {
//                 for (int k = 1; k < NZ - max_step_h; ++k) {
//                     // 尝试尺寸组合
//                     for (int sl = step_l; sl <= max_step_l; ++sl) {
//                         for (int sw = step_w; sw <= max_step_w; ++sw) {
//                             for (int sh = step_h; sh <= max_step_h; ++sh) {
//                                 int i2 = i + sl, j2 = j + sw, k2 = k + sh;
//                                 // 积分图 O(1) 查询：利用 8 个顶点计算内部总和
//                                 int count = sum[i2*NY*NZ + j2*NZ + k2]
//                                           - sum[i*NY*NZ + j2*NZ + k2]
//                                           - sum[i2*NY*NZ + j*NZ + k2]
//                                           - sum[i2*NY*NZ + j2*NZ + k]
//                                           + sum[i*NY*NZ + j*NZ + k2]
//                                           + sum[i*NY*NZ + j2*NZ + k]
//                                           + sum[i2*NY*NZ + j*NZ + k]
//                                           - sum[i*NY*NZ + j*NZ + k];

//                                 double vol = (sl * RES) * (sw * RES) * (sh * RES);
//                                 double d = count / vol;
//                                 if (d > local_best.density) {
//                                     local_best.density = d;
//                                     local_best.point_count = count;
//                                     local_best.min_x = x_min + (i-1)*RES;
//                                     local_best.max_x = x_min + i2*RES;
//                                     // ... 省略其他坐标赋值 ...
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//         #pragma omp critical
//         if (local_best.density > best.density) best = local_best;
//     }

//     std::cout << "最优密度: " << best.density << " pts/m³" << std::endl;
//     return 0;
// }

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <sstream>
// #include <map>
// #include <algorithm>
// #include <cmath>

// struct Point {
//     double x, y, z;
// };

// // 记录最优框的结构
// struct BestBox {
//     double min_x, max_x, min_y, max_y, min_z, max_z;
//     double density = 0;
//     int point_count = 0;
// };

// // 空间哈希函数，将坐标转为唯一字符串 key
// std::string get_key(int ix, int iy, int iz) {
//     return std::to_string(ix) + "," + std::to_string(iy) + "," + std::to_string(iz);
// }

// int main() {
//     // ====================== 核心约束配置 ======================
//     const std::string input_file = "output.txt";
    
//     // 尺寸限制 (单位: 米)
//     const double MIN_DIM = 0.2;   
//     const double MAX_DIM = 0.4;   

//     // 搜索的分辨率（也是 Voxel 的大小）
//     const double RES = 0.1; 
//     // =========================================================

//     std::ifstream infile(input_file);
//     if (!infile) {
//         std::cerr << "文件打开失败！" << std::endl;
//         return -1;
//     }

//     // 使用 Map 存储每个 Voxel 里的点数，实现 VoxelGrid
//     std::map<std::string, int> voxel_grid;
//     std::string line;

//     std::cout << "正在进行 VoxelGrid 粗略统计..." << std::endl;
//     while (std::getline(infile, line)) {
//         if (line.empty()) continue;
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         Point p;
//         if (ss >> p.x >> p.y >> p.z) {
//             int ix = std::floor(p.x / RES);
//             int iy = std::floor(p.y / RES);
//             int iz = std::floor(p.z / RES);
//             voxel_grid[get_key(ix, iy, iz)]++;
//         }
//     }

//     BestBox best;
    
//     // 转换：将 Map 转为易于遍历的坐标列表，只遍历“有点”的地方
//     struct VoxelPos { int x, y, z; int count; };
//     std::vector<VoxelPos> active_voxels;
//     for (auto const& item : voxel_grid) {
//         std::stringstream ss(item.first);
//         int x, y, z;
//         char c;
//         ss >> x >> c >> y >> c >> z;
//         active_voxels.push_back({x, y, z, item.second});
//     }

//     std::cout << "有效数据块数量: " << active_voxels.size() << "。开始寻找密度最高立体框..." << std::endl;

//     // 只从“有点”的 Voxel 开始尝试作为框的起点
//     for (const auto& seed : active_voxels) {
//         double start_x = seed.x * RES;
//         double start_y = seed.y * RES;
//         double start_z = seed.z * RES;

//         // 尝试不同的尺寸 (符合 MIN 和 MAX 约束)
//         for (double cur_w = MIN_DIM; cur_w <= MAX_DIM; cur_w += RES) {
//             for (double cur_h = MIN_DIM; cur_h <= MAX_DIM; cur_h += RES) {
//                 for (double cur_d = MIN_DIM; cur_d <= MAX_DIM; cur_d += RES) {
                    
//                     int current_box_points = 0;
                    
//                     // 核心优化：只遍历当前框覆盖到的 Voxel
//                     int start_ix = seed.x;
//                     int end_ix = seed.x + std::floor(cur_w / RES);
//                     int start_iy = seed.y;
//                     int end_iy = seed.y + std::floor(cur_h / RES);
//                     int start_iz = seed.z;
//                     int end_iz = seed.z + std::floor(cur_d / RES);

//                     for (int ix = start_ix; ix <= end_ix; ++ix) {
//                         for (int iy = start_iy; iy <= end_iy; ++iy) {
//                             for (int iz = start_iz; iz <= end_iz; ++iz) {
//                                 std::string key = get_key(ix, iy, iz);
//                                 if (voxel_grid.count(key)) {
//                                     current_box_points += voxel_grid[key];
//                                 }
//                             }
//                         }
//                     }

//                     double volume = cur_w * cur_h * cur_d;
//                     double density = current_box_points / volume;

//                     if (density > best.density) {
//                         best.density = density;
//                         best.point_count = current_box_points;
//                         best.min_x = start_x; best.max_x = start_x + cur_w;
//                         best.min_y = start_y; best.max_y = start_y + cur_h;
//                         best.min_z = start_z; best.max_z = start_z + cur_d;
//                     }
//                 }
//             }
//         }
//     }

//     // ====================== 结果报告 ======================
//     std::cout << "\n================ 发现最优立体框 (Voxel 加速版) ================" << std::endl;
//     std::cout << "物理边界 X: [" << best.min_x << " 至 " << best.max_x << "]" << std::endl;
//     std::cout << "物理边界 Y: [" << best.min_y << " 至 " << best.max_y << "]" << std::endl;
//     std::cout << "物理边界 Z: [" << best.min_z << " 至 " << best.max_z << "]" << std::endl;
//     std::cout << "框内点数: " << best.point_count << std::endl;
//     std::cout << "单位体积密度: " << best.density << " pts/m³" << std::endl;

//     return 0;
// }

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <sstream>  // 必须包含这个处理 stringstream
// #include <map>
// #include <algorithm>
// #include <cmath>

// // g++ -O3 analyze_precision.cpp -o analyze_precision -std=c++11
// // ./analyze_precision

// struct Point {
//     double x, y, z;
// };

// struct Voxel {
//     std::vector<Point> points;
//     double actual_min_x = 1e9, actual_max_x = -1e9;
//     double actual_min_y = 1e9, actual_max_y = -1e9;
//     double actual_min_z = 1e9, actual_max_z = -1e9;

//     void addPoint(const Point& p) {
//         points.push_back(p);
//         if (p.x < actual_min_x) actual_min_x = p.x; if (p.x > actual_max_x) actual_max_x = p.x;
//         if (p.y < actual_min_y) actual_min_y = p.y; if (p.y > actual_max_y) actual_max_y = p.y;
//         if (p.z < actual_min_z) actual_min_z = p.z; if (p.z > actual_max_z) actual_max_z = p.z;
//     }
// };

// int main() {
//     // ====================== 核心配置 ======================
//     const std::string input_file = "output.txt";
//     const std::string report_file = "plane_discovery_report.txt";

//     // 你唯一需要控制的：允许的平面厚度 (单位: 米)
//     const double THICKNESS_TOLERANCE = 1.00; 
    
//     // 探测分辨率 (RES 越小，对平面边界的探测越精细)
//     const double RES = 0.15; 
//     const int MIN_POINTS_CLUSTER = 500; 
//     // =====================================================

//     std::ifstream infile(input_file);
//     std::ofstream report(report_file);
    
//     if (!infile || !report) {
//         std::cerr << "文件打开失败，请检查路径！" << std::endl;
//         return -1;
//     }

//     std::map<std::string, Voxel> grid;
//     std::string line;

//     std::cout << "开始扫描点云特征..." << std::endl;

//     while (std::getline(infile, line)) {
//         if (line.empty()) continue;
//         // 兼容处理逗号
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         Point p;
//         if (ss >> p.x >> p.y >> p.z) {
//             int ix = std::floor(p.x / RES);
//             int iy = std::floor(p.y / RES);
//             int iz = std::floor(p.z / RES);
//             std::string key = std::to_string(ix) + "," + std::to_string(iy) + "," + std::to_string(iz);
//             grid[key].addPoint(p);
//         }
//     }

//     report << "==========================================================\n";
//     report << "            自动平面特征发现与空间范围分析报告\n";
//     report << "==========================================================\n";
//     report << "设定容忍厚度: " << THICKNESS_TOLERANCE * 1000 << " mm\n\n";

//     int count = 0;
//     // 使用兼容性写法，避免 C++17 警告
//     for (auto const& item : grid) {
//         const Voxel& v = item.second;
//         if (v.points.size() < MIN_POINTS_CLUSTER) continue;

//         double dx = v.actual_max_x - v.actual_min_x;
//         double dy = v.actual_max_y - v.actual_min_y;
//         double dz = v.actual_max_z - v.actual_min_z;
//         double thickness = std::min({dx, dy, dz});

//         if (thickness <= THICKNESS_TOLERANCE) {
//             count++;
//             report << "【平面特征 #" << count << "】\n";
//             report << "  - 物理边界 (X): [" << v.actual_min_x << " 至 " << v.actual_max_x << "] 长度: " << dx << "m\n";
//             report << "  - 物理边界 (Y): [" << v.actual_min_y << " 至 " << v.actual_max_y << "] 宽度: " << dy << "m\n";
//             report << "  - 物理边界 (Z): [" << v.actual_min_z << " 至 " << v.actual_max_z << "] 高度跨度: " << dz << "m\n";
            
//             report << "  - 表面精度 (实测厚度): " << thickness * 1000 << " mm\n";
            
//             report << "  - 空间姿态分析: ";
//             if (thickness == dz) report << "该平面处于 [水平] 姿态。\n";
//             else if (thickness == dx) report << "该平面为 [垂直于X轴] 的立面。\n";
//             else report << "该平面为 [垂直于Y轴] 的立面。\n";
            
//             report << "  - 采样密度: " << v.points.size() << " 个点\n";
//             report << "----------------------------------------------------------\n\n";
//         }
//     }

//     report << "结论: 共自动识别出 " << count << " 个符合厚度条件的平面区域。\n";
//     report.close();
//     std::cout << "分析完成，报告已生成至 " << report_file << std::endl;

//     return 0;
// }

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <vector>
// #include <sstream>
// #include <map>
// #include <algorithm>
// #include <iomanip>
// #include <cmath>

// struct Point {
//     double x, y, z;
// };

// struct Voxel {
//     std::vector<Point> points;
//     double min_x = 1e9, max_x = -1e9;
//     double min_y = 1e9, max_y = -1e9;
//     double min_z = 1e9, max_z = -1e9;

//     void addPoint(const Point& p) {
//         points.push_back(p);
//         if (p.x < min_x) min_x = p.x; if (p.x > max_x) max_x = p.x;
//         if (p.y < min_y) min_y = p.y; if (p.y > max_y) max_y = p.y;
//         if (p.z < min_z) min_z = p.z; if (p.z > max_z) max_z = p.z;
//     }
// };

// int main() {
//     // ====================== 配置区域 ======================
//     const std::string dir = "/home/ztl/program/boxfiles/LiDAR/Unitree_LIO_source_code/catkin_point_lio_unilidar/src/point_lio_unilidar/PCD/";
//     const std::string input_file = "output.txt";
//     const std::string report_file = "output_report.txt";

//     // 搜索格子的步长（决定了分析的精细程度）
//     const double VX = 12.00;  
//     const double VY = 8.00;
//     const double VZ = 0.04; 

//     // 平面判定阈值：如果某方向厚度小于此值，视为平面候选
//     const double THICKNESS_THRESHOLD = 0.05; // 5厘米以内视为“薄面”
//     const int MIN_POINTS = 80;               // 至少80个点才分析
//     // =====================================================

//     std::ifstream infile(dir + input_file);
//     std::ofstream report(dir + report_file);

//     if (!infile.is_open() || !report.is_open()) {
//         std::cerr << "文件打开失败！" << std::endl;
//         return -1;
//     }

//     std::map<std::string, Voxel> grid;
//     std::string line;

//     std::cout << "正在读取数据并生成深度报告..." << std::endl;

//     while (std::getline(infile, line)) {
//         if (line.empty()) continue;
//         for (auto &ch : line) if (ch == ',') ch = ' ';
//         std::stringstream ss(line);
//         Point p;
//         if (ss >> p.x >> p.y >> p.z) {
//             int ix = static_cast<int>(std::floor(p.x / VX));
//             int iy = static_cast<int>(std::floor(p.y / VY));
//             int iz = static_cast<int>(std::floor(p.z / VZ));
//             std::string key = std::to_string(ix) + "," + std::to_string(iy) + "," + std::to_string(iz);
//             grid[key].addPoint(p);
//         }
//     }

//     // 写入报告头部
//     report << "==========================================================\n";
//     report << "            雷达点云精度与平面特征深度分析报告\n";
//     report << "==========================================================\n\n";
//     report << "分析参数设定:\n";
//     report << "- 局部搜索步长: X=" << VX << "m, Y=" << VY << "m, Z=" << VZ << "m\n";
//     report << "- 平面厚度阈值: < " << THICKNESS_THRESHOLD << "m\n";
//     report << "- 最小有效点数: " << MIN_POINTS << " points\n\n";

//     int plane_count = 0;
//     // g++ -O3 analyze_precision.cpp -o analyze_precision -std=c++17
//     // for (auto const& [key, v] : grid) {
//     for (auto const& item : grid) {
//     const std::string& key = item.first;
//     const Voxel& v = item.second;
    
//         if (v.points.size() < MIN_POINTS) continue;

//         double dx = v.max_x - v.min_x;
//         double dy = v.max_y - v.min_y;
//         double dz = v.max_z - v.min_z;
//         double thickness = std::min({dx, dy, dz});

//         if (thickness <= THICKNESS_THRESHOLD) {
//             plane_count++;
//             report << "【发现疑似平面区域 #" << plane_count << "】\n";
//             report << "  - 空间范围定位:\n";
//             report << "    X轴: [" << v.min_x << " 至 " << v.max_x << "] 跨度: " << dx << "m\n";
//             report << "    Y轴: [" << v.min_y << " 至 " << v.max_y << "] 跨度: " << dy << "m\n";
//             report << "    Z轴: [" << v.min_z << " 至 " << v.max_z << "] 跨度: " << dz << "m\n";
            
//             // 特点描述
//             report << "  - 几何特点描述: ";
//             if (thickness == dz) report << "该区域表现为一个[水平面]特征。";
//             else if (thickness == dx) report << "该区域表现为一个[垂直于X轴的立面]特征。";
//             else report << "该区域表现为一个[垂直于Y轴的立面]特征。";
            
//             report << "\n  - 精度评估: 当前区域点云厚度(误差)约为 " << thickness * 1000 << " mm。\n";
//             report << "  - 数据密度: 共有 " << v.points.size() << " 个采样点在此小范围内聚集。\n";
//             report << "----------------------------------------------------------\n\n";
//         }
//     }

//     report << "总结: 在当前点云数据中，共识别出 " << plane_count << " 处高精度平面特征区域。\n";
//     report.close();

//     std::cout << "报告生成成功！请查看: " << dir + report_file << std::endl;

//     return 0;
// }

// /*

// */