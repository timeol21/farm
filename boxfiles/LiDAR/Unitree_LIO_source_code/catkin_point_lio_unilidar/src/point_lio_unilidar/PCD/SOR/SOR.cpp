

// #include <iostream>   //有多线程逻辑
// #include <fstream>
// #include <vector>
// #include <pcl/point_types.h>
// #include <pcl/filters/extract_indices.h>
// #include <pcl/segmentation/sac_segmentation.h>
// #include <pcl/surface/mls.h>
// #include <pcl/common/common.h>
// #include <omp.h> // 必须引入 OpenMP 头文件

// // g++ -O3 multi_surface_analysis_parallel.cpp -o surface_eval \
// // -lpcl_common -lpcl_filters -lpcl_surface -lpcl_segmentation -lpcl_search \
// // -fopenmp

// typedef pcl::PointXYZ PointT;

// int main() {
//     std::string input_file = "../scans_ALL.txt";
//     pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>);

//     // 1. 加载点云数据 (IO阶段通常是单线程，受硬盘速度限制)
//     std::ifstream infile(input_file);
//     if (!infile) { std::cerr << "无法打开文件" << std::endl; return -1; }
//     std::string line;
//     while (std::getline(infile, line)) {
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         PointT p;
//         if (ss >> p.x >> p.y >> p.z) cloud->push_back(p);
//     }
//     std::cout << "点云加载完成，总点数: " << cloud->size() << std::endl;

//     // 2. MLS 自动平滑与补点 (这是最耗时的步骤，启用 PCL 内置多线程)
//     pcl::PointCloud<PointT>::Ptr mls_cloud(new pcl::PointCloud<PointT>);
//     pcl::MovingLeastSquares<PointT, PointT> mls;
//     pcl::search::KdTree<PointT>::Ptr tree(new pcl::search::KdTree<PointT>);
    
//     mls.setInputCloud(cloud);
//     mls.setPolynomialOrder(2);
//     mls.setSearchMethod(tree);
//     mls.setSearchRadius(0.05);
    
//     // --- 多线程优化点 1: 启用 PCL MLS 的内部多线程 ---
//     mls.setNumberOfThreads(4); // 针对 RK356X 的 4 核
    
//     mls.setUpsamplingMethod(pcl::MovingLeastSquares<PointT, PointT>::SAMPLE_LOCAL_PLANE);
//     mls.setUpsamplingStepSize(0.01);
    
//     std::cout << "正在执行多线程 MLS 曲面平滑..." << std::endl;
//     mls.process(*mls_cloud);

//     // 3. 准备结果输出文件
//     std::ofstream res_file("plane_analysis_results.txt");
//     res_file << "ID, 平面方程(Ax+By+Cz+D=0), 包含点数, 平整度(RMS/mm)\n";

//     // 4. 全自动迭代提取所有平面
//     pcl::SACSegmentation<PointT> seg;
//     pcl::ExtractIndices<PointT> extract;
//     pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
//     pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
    
//     seg.setOptimizeCoefficients(true);
//     seg.setModelType(pcl::SACMODEL_PLANE);
//     seg.setMethodType(pcl::SAC_RANSAC);
//     seg.setDistanceThreshold(0.1);

//     int plane_count = 0;
//     pcl::PointCloud<PointT>::Ptr remaining_cloud = mls_cloud;
//     int original_size = (int)remaining_cloud->size();

//     while (remaining_cloud->size() > 0.2 * original_size) {
//         seg.setInputCloud(remaining_cloud);
//         seg.segment(*inliers, *coefficients);

//         if (inliers->indices.size() < 500) break;

//         // --- 多线程优化点 2: 使用 OpenMP 并行计算残差 ---
//         double sum_sq_dist = 0;
//         double a = coefficients->values[0], b = coefficients->values[1], 
//                c = coefficients->values[2], d = coefficients->values[3];
//         double norm = std::sqrt(a*a + b*b + c*c);

//         int n_inliers = (int)inliers->indices.size();
        
//         // 并行化 for 循环，使用 reduction 规约求和防止竞争
//         #pragma omp parallel for reduction(+:sum_sq_dist)
//         for (int i = 0; i < n_inliers; ++i) {
//             int idx = inliers->indices[i];
//             PointT p = remaining_cloud->points[idx];
//             double dist = std::abs(a*p.x + b*p.y + c*p.z + d) / norm;
//             sum_sq_dist += dist * dist;
//         }
        
//         double rms_mm = std::sqrt(sum_sq_dist / n_inliers) * 1000.0;

//         plane_count++;
//         res_file << plane_count << ", " 
//                  << a << "x + " << b << "y + " << c << "z + " << d << " = 0, "
//                  << n_inliers << ", " 
//                  << rms_mm << "\n";

//         extract.setInputCloud(remaining_cloud);
//         extract.setIndices(inliers);
//         extract.setNegative(true);
//         pcl::PointCloud<PointT>::Ptr temp(new pcl::PointCloud<PointT>);
//         extract.filter(*temp);
//         remaining_cloud = temp;
//     }

//     std::cout << "分析完成，共识别出 " << plane_count << " 个平面区域。" << std::endl;
//     return 0;
// }

// #include <iostream>   //自动化
// #include <pcl/point_types.h>
// #include <pcl/filters/statistical_outlier_removal.h>
// #include <pcl/features/normal_3d.h>
// #include <pcl/segmentation/sac_segmentation.h>
// #include <pcl/surface/mls.h>

// typedef pcl::PointXYZ PointT;

// struct AnalysisResult {
//     std::string shape_type;
//     double fitness_score; // RMS 误差
//     int inlier_count;
// };

// int main() {
//     // [1] 加载与预处理 (SOR 与 MLS 平滑)
//     // 建议沿用之前代码中的 MLS 部分，因为平滑后的点云对于形状识别更准确
    
//     // 准备数据结构
//     pcl::PointCloud<pcl::PointNormal>::Ptr processed_cloud(new pcl::PointCloud<pcl::PointNormal>);
//     // ... (此处省略 MLS 处理逻辑，假设已得到 processed_cloud)

//     // [2] 多模型竞争检测
    
//     // --- 尝试平面拟合 ---
//     pcl::SACSegmentationFromNormals<pcl::PointNormal, pcl::PointNormal> seg;
//     pcl::ModelCoefficients::Ptr coeff_plane(new pcl::ModelCoefficients);
//     pcl::PointIndices::Ptr inliers_plane(new pcl::PointIndices);
    
//     seg.setOptimizeCoefficients(true);
//     seg.setModelType(pcl::SACMODEL_NORMAL_PLANE); // 使用带法线的平面模型
//     seg.setNormalDistanceWeight(0.1);
//     seg.setMethodType(pcl::SAC_RANSAC);
//     seg.setMaxIterations(1000);
//     seg.setDistanceThreshold(0.02); // 2cm 阈值
//     seg.setInputCloud(processed_cloud);
//     seg.setInputNormals(processed_cloud);
//     seg.segment(*inliers_plane, *coeff_plane);

//     // --- 尝试圆柱拟合 ---
//     pcl::ModelCoefficients::Ptr coeff_cyl(new pcl::ModelCoefficients);
//     pcl::PointIndices::Ptr inliers_cyl(new pcl::PointIndices);
    
//     seg.setModelType(pcl::SACMODEL_CYLINDER); // 切换为圆柱模型
//     seg.setRadiusLimits(0.1, 5.0); // 假设罐体半径在 0.1m 到 5m 之间
//     seg.segment(*inliers_cyl, *coeff_cyl);

//     // [3] 胜出机制：决策逻辑
//     std::string final_shape = "Unknown";
//     if (inliers_plane->indices.size() > inliers_cyl->indices.size()) {
//         final_shape = "Plane";
//         std::cout << "检测结果：该物体自动识别为 [平面] (天花板/墙面)" << std::endl;
//         // 执行平面平整度计算公式
//     } else {
//         final_shape = "Cylinder";
//         std::cout << "检测结果：该物体自动识别为 [圆柱体] (工业罐体)" << std::endl;
//         // 执行圆柱残差计算逻辑
//     }

//     return 0;
// }

// #include <iostream>          //用来去噪和OBB估计
// #include <fstream>
// #include <vector>
// #include <string>
// #include <algorithm>
// #include <pcl/point_types.h>
// #include <pcl/filters/statistical_outlier_removal.h>
// #include <pcl/common/common.h>
// #include <pcl/features/moment_of_inertia_estimation.h> // 必须包含这个头文件用于OBB计算

// typedef pcl::PointXYZ PointT;

// int main() {
//     std::string input_file = "./denoised_output.txt";
//     std::string output_file = "denoised_output1.txt";

//     // 1. 加载 TXT 数据
//     pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>);
//     std::ifstream infile(input_file);
//     if (!infile) {
//         std::cerr << "错误：无法打开输入文件 " << input_file << std::endl;
//         return -1;
//     }

//     std::string line;
//     while (std::getline(infile, line)) {
//         if (line.empty()) continue;
//         std::replace(line.begin(), line.end(), ',', ' ');
//         std::stringstream ss(line);
//         PointT p;
//         if (ss >> p.x >> p.y >> p.z) cloud->push_back(p);
//     }
//     std::cout << "原始点云数量: " << cloud->size() << std::endl;

//     // 2. SOR 统计去噪 (对应你在 CloudCompare 中的参数)
//     pcl::PointCloud<PointT>::Ptr cloud_filtered(new pcl::PointCloud<PointT>);
//     pcl::StatisticalOutlierRemoval<PointT> sor;
//     sor.setInputCloud(cloud);
//     sor.setMeanK(6);            // 邻居数
//     sor.setStddevMulThresh(1.0); // nSigma
//     sor.filter(*cloud_filtered);
//     std::cout << "去噪后点云数量: " << cloud_filtered->size() << std::endl;

//     // 3. 物理特征评估 (利用 OBB 算法)
//     // OBB (Oriented Bounding Box) 能在物体旋转的情况下依然计算出真实的长宽高
//     pcl::MomentOfInertiaEstimation<PointT> feature_extractor;
//     feature_extractor.setInputCloud(cloud_filtered);
//     feature_extractor.compute();

//     PointT min_point_OBB, max_point_OBB, position_OBB;
//     Eigen::Matrix3f rotational_matrix_OBB;
//     // 获取导向包围盒参数
//     feature_extractor.getOBB(min_point_OBB, max_point_OBB, position_OBB, rotational_matrix_OBB);

//     // 计算物体真实尺寸（不受扫描角度旋转影响）
//     double length = max_point_OBB.x - min_point_OBB.x;
//     double width  = max_point_OBB.y - min_point_OBB.y;
//     double height = max_point_OBB.z - min_point_OBB.z;

//     // 4. 特征评估报告
//     std::cout << "\n================ 几何特征识别报告 ================" << std::endl;
//     std::cout << "物体实际尺寸: " << length << "m(长) x " << width << "m(宽) x " << height << "m(高)" << std::endl;
    
//     // 形状猜测逻辑
//     std::vector<double> dims = {length, width, height};
//     std::sort(dims.begin(), dims.end()); // 排序：小，中，大

//     if ((dims[1] - dims[0]) < 0.05 && (dims[2] - dims[1]) < 0.05) {
//         std::cout << "初步判定: 该物体几何特征接近 [正方体]" << std::endl;
//     } else if (std::abs(dims[0] - dims[1]) < 0.08) {
//         // 如果长宽接近且高度明显不同
//         std::cout << "初步判定: 该物体几何特征接近 [圆柱体/长方体柱]" << std::endl;
//         std::cout << "底面估算直径/边长: " << (dims[0] + dims[1]) / 2.0 << "m, 高度: " << dims[2] << "m" << std::endl;
//     } else {
//         std::cout << "初步判定: 该物体几何特征为标准 [长方体]" << std::endl;
//     }
//     std::cout << "==================================================\n" << std::endl;

//     // 5. 保存去噪后的数据为 TXT
//     std::ofstream outfile(output_file);
//     for (const auto& p : cloud_filtered->points) {
//         outfile << p.x << "," << p.y << "," << p.z << "\n";
//     }
//     std::cout << "处理完成！去噪后的点云已保存至: " << output_file << std::endl;

//     return 0;
// }


// #include <pcl/sample_consensus/method_types.h>
// #include <pcl/sample_consensus/model_types.h>
// #include <pcl/segmentation/sac_segmentation.h>
// #include <pcl/common/common.h>  // RANSAC 拟合

// // 假设 cloud 是你 SOR 去噪后的点云
// void evaluateSurfaceFlatness(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud) {
//     pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
//     pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
    
//     // 1. 创建分割对象
//     pcl::SACSegmentation<pcl::PointXYZ> seg;
//     seg.setOptimizeCoefficients(true);
//     seg.setModelType(pcl::SACMODEL_PLANE); // 如果是罐体侧面，改用 SACMODEL_CYLINDER
//     seg.setMethodType(pcl::SAC_RANSAC);
//     seg.setDistanceThreshold(0.01); // 允许的误差范围

//     seg.setInputCloud(cloud);
//     seg.segment(*inliers, *coefficients);

//     if (inliers->indices.empty()) return;

//     // 2. 计算平整度 (残差分析)
//     double sum_sq_dist = 0;
//     for (int idx : inliers->indices) {
//         pcl::PointXYZ p = cloud->points[idx];
//         // 计算点到理想平面的距离: |Ax+By+Cz+D| / sqrt(A^2+B^2+C^2)
//         double dist = std::abs(coefficients->values[0]*p.x + 
//                                coefficients->values[1]*p.y + 
//                                coefficients->values[2]*p.z + 
//                                coefficients->values[3]);
//         sum_sq_dist += dist * dist;
//     }

//     double flatness_score = std::sqrt(sum_sq_dist / inliers->indices.size());
//     std::cout << "该表面的平整度 (标准差): " << flatness_score << " 米" << std::endl;
    
//     if (flatness_score > 0.02) { // 假设超过2cm就不合格
//         std::cout << "警报：表面检测到明显坑洼！" << std::endl;
//     }
// }