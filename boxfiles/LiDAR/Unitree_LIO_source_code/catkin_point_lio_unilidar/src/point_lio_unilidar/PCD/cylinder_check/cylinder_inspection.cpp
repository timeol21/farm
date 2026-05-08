/*
mkdir build && cd build
cmake ..
make
./cylinder_inspection
*/


#include <iostream>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/features/normal_3d.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/common/common.h>
#include <vector>
#include <map>
#include <cmath>

typedef pcl::PointXYZI PointT; // 使用带有强度信息的点，我们将用Intensity存储偏差

int main(int argc, char** argv) {
    // 1. 加载点云
    pcl::PointCloud<PointT>::Ptr cloud(new pcl::PointCloud<PointT>);
    if (pcl::io::loadPCDFile<PointT>("../scans.pcd", *cloud) == -1) {
        std::cerr << "无法找到 scans.pcd" << std::endl;
        return -1;
    }

    // 2. 去噪：低精度雷达必须先滤波
    pcl::StatisticalOutlierRemoval<PointT> sor;
    sor.setInputCloud(cloud);
    sor.setMeanK(50);
    sor.setStddevMulThresh(1.0);
    sor.filter(*cloud);

    // 3. 计算法向量 (圆柱拟合必备)
    pcl::NormalEstimation<PointT, pcl::Normal> ne;
    pcl::search::KdTree<PointT>::Ptr tree(new pcl::search::KdTree<PointT>());
    pcl::PointCloud<pcl::Normal>::Ptr cloud_normals(new pcl::PointCloud<pcl::Normal>);
    ne.setSearchMethod(tree);
    ne.setInputCloud(cloud);
    ne.setKSearch(50);
    ne.compute(*cloud_normals);

    // 4. RANSAC 拟合圆柱
    pcl::SACSegmentationFromNormals<PointT, pcl::Normal> seg;
    pcl::ModelCoefficients::Ptr coefficients_cyl(new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers_cyl(new pcl::PointIndices);

    seg.setOptimizeCoefficients(true);
    seg.setModelType(pcl::SACMODEL_CYLINDER);
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setNormalDistanceWeight(0.1);
    seg.setMaxIterations(10000);
    seg.setDistanceThreshold(0.05);      // 5cm内的点参与拟合
    seg.setRadiusLimits(16.5, 17.5);     // 目标半径17m
    seg.setInputCloud(cloud);
    seg.setInputNormals(cloud_normals);
    seg.segment(*inliers_cyl, *coefficients_cyl);

    if (inliers_cyl->indices.empty()) {
        std::cerr << "未能拟合出圆柱体结构！" << std::endl;
        return -1;
    }

    // 提取拟合参数
    float x0 = coefficients_cyl->values[0];
    float y0 = coefficients_cyl->values[1];
    float R_fit = coefficients_cyl->values[6];
    std::cout << "--- 拟合报告 ---" << std::endl;
    std::cout << "拟合圆心位置: X=" << x0 << ", Y=" << y0 << std::endl;
    std::cout << "拟合半径: " << R_fit << " 米 (目标: 17.0m)" << std::endl;

    // 5. 残差分析与 Log 统计
    // 使用 map 按 1米 高度对偏差进行分组
    std::map<int, std::vector<float>> layer_stats;
    
    for (auto& point : cloud->points) {
        // 计算当前点到轴线的距离
        float dist_to_axis = std::sqrt(std::pow(point.x - x0, 2) + std::pow(point.y - y0, 2));
        // 偏差 = 实际距离 - 拟合半径
        float deviation = dist_to_axis - R_fit;
        
        // 将偏差存入 Intensity 字段，方便在 CloudCompare 中着色
        point.intensity = deviation;

        // 按高度层统计
        int layer = std::floor(point.z);
        layer_stats[layer].push_back(std::abs(deviation));
    }

    // 6. 输出平整度 Log
    std::cout << "\n--- 分层平整度报告 (RMSE指标) ---" << std::endl;
    for (auto const& [height, devs] : layer_stats) {
        float sum_sq = 0;
        float max_dev = 0;
        for (float d : devs) {
            sum_sq += d * d;
            if (d > max_dev) max_dev = d;
        }
        float rmse = std::sqrt(sum_sq / devs.size());
        printf("[高度 %2dm - %2dm] 点数: %5zu | RMSE: %.4fm | 最大偏差: %.4fm\n", 
                height, height+1, devs.size(), rmse, max_dev);
    }

    // 7. 保存带有偏差信息的点云
    pcl::io::savePCDFileBinary("inspection_result.pcd", *cloud);
    std::cout << "\n处理完成！请将 inspection_result.pcd 导入 CloudCompare。" << std::endl;

    return 0;
}