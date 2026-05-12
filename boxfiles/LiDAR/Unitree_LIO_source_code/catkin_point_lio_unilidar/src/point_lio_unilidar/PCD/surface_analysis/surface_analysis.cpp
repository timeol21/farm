#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/surface/mls.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/filters/statistical_outlier_removal.h>

typedef pcl::PointXYZ PointT;

int main() {
    std::string input_file = "output.txt";
    pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>);

    // 1. 读取原始 TXT
    std::ifstream infile(input_file);
    if (!infile) { std::cerr << "无法打开文件!" << std::endl; return -1; }
    std::string line;
    while (std::getline(infile, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::stringstream ss(line);
        PointT p;
        if (ss >> p.x >> p.y >> p.z) cloud->push_back(p);
    }

    // 2. SOR 去噪 (工业预处理第一步)
    pcl::PointCloud<PointT>::Ptr cloud_sor(new pcl::PointCloud<PointT>);
    pcl::StatisticalOutlierRemoval<PointT> sor;
    sor.setInputCloud(cloud);
    sor.setMeanK(20);
    sor.setStddevMulThresh(1.0);
    sor.filter(*cloud_sor);

    // 3. MLS 移动最小二乘法：实现“点云转平滑曲面”
    // 这步会解决你说的 (x,y) 对应唯一 Z 轴趋势的问题，并自动补洞
    pcl::PointCloud<pcl::PointNormal>::Ptr mls_points(new pcl::PointCloud<pcl::PointNormal>);
    pcl::MovingLeastSquares<PointT, pcl::PointNormal> mls;
    pcl::search::KdTree<PointT>::Ptr tree(new pcl::search::KdTree<PointT>);
    
    mls.setComputeNormals(true);
    mls.setInputCloud(cloud_sor);
    mls.setPolynomialOrder(2); // 二次多项式拟合局部曲面
    mls.setSearchMethod(tree);
    mls.setSearchRadius(0.05); // 搜索半径 5cm
    
    // 关键：重采样设置（实现自动找相邻点补齐）
    mls.setUpsamplingMethod(pcl::MovingLeastSquares<PointT, pcl::PointNormal>::SAMPLE_LOCAL_PLANE);
    mls.setUpsamplingStepSize(0.01); // 每隔 1cm 重新采样一个点
    mls.process(*mls_points);
    std::cout << "MLS 曲面重建完成，点数: " << mls_points->size() << std::endl;

    // 4. RANSAC 拟合理想平面 (参考基准面)
    pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
    pcl::SACSegmentation<pcl::PointNormal> seg;
    seg.setOptimizeCoefficients(true);
    seg.setModelType(pcl::SACMODEL_PLANE);
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setDistanceThreshold(0.01);
    seg.setInputCloud(mls_points);
    seg.segment(*inliers, *coefficients);

    // 5. 计算平整度（残差分析）
    double total_sq_error = 0;
    double A = coefficients->values[0], B = coefficients->values[1], 
           C = coefficients->values[2], D = coefficients->values[3];
    double norm = std::sqrt(A*A + B*B + C*C);

    // 将结果存入 txt，包含 (x,y,z,偏差)
    std::ofstream outfile("surface_result.txt");
    for (const auto& p : mls_points->points) {
        // 计算点到理想平面的垂直距离（即坑洼深度）
        double dist = (A*p.x + B*p.y + C*p.z + D) / norm;
        total_sq_error += dist * dist;
        outfile << p.x << "," << p.y << "," << p.z << "," << dist << "\n";
    }

    double flatness = std::sqrt(total_sq_error / mls_points->size());
    std::cout << "====================================" << std::endl;
    std::cout << "拟合平面方程: " << A << "x + " << B << "y + " << C << "z + " << D << " = 0" << std::endl;
    std::cout << "表面平整度 (RMS 误差): " << flatness * 1000 << " mm" << std::endl;
    if (flatness > 0.005) std::cout << "结论：检测到表面存在显著坑洼/形变" << std::endl;
    else std::cout << "结论：表面平整度符合要求" << std::endl;
    std::cout << "====================================" << std::endl;

    return 0;
}