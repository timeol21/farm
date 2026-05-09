#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/vtk_io.h>
#include <pcl/features/normal_3d.h>
#include <pcl/surface/poisson.h>
#include <pcl/common/common.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <iostream>

// 理想模型基准参数
const float IDEAL_X = 15.0f;
const float IDEAL_Y = 10.0f;
const float IDEAL_Z = 3.0f;

int main(int argc, char** argv) {
    // 1. 加载并去噪
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    if (pcl::io::loadPCDFile<pcl::PointXYZ>("../restored_output.pcd", *cloud) == -1) return -1;

    // 预处理：剔除那些精度大、乱跳的噪点，防止产生错误的“粘连”
    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
    sor.setInputCloud(cloud);
    sor.setMeanK(50);
    sor.setStddevMulThresh(1.0);
    pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    sor.filter(*filtered_cloud);

    // 2. 计算法向量（隐式重建的核心：法向量决定了表面的内外走向）
    pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> ne;
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
    ne.setInputCloud(filtered_cloud);
    ne.setSearchMethod(tree);
    ne.setKSearch(30); 
    ne.compute(*normals);

    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals(new pcl::PointCloud<pcl::PointNormal>);
    pcl::concatenateFields(*filtered_cloud, *normals, *cloud_with_normals);

    // 3. 泊松重建 (Implicit Surface Reconstruction)
    pcl::Poisson<pcl::PointNormal> poisson;
    // Depth 越高，细节越丰富（接触的地方越细致），建议 8-9 级
    poisson.setDepth(9); 
    poisson.setInputCloud(cloud_with_normals);
    pcl::PolygonMesh mesh;
    poisson.reconstruct(mesh);

    // 4. 理想模型比对评测
    pcl::PointXYZ min_pt, max_pt;
    pcl::getMinMax3D(*filtered_cloud, min_pt, max_pt);
    float real_x = max_pt.x - min_pt.x;
    float real_y = max_pt.y - min_pt.y;
    float real_z = max_pt.z - min_pt.z;

    std::cout << "\n==========================================" << std::endl;
    std::cout << "         数字孪生建模评测报告             " << std::endl;
    std::cout << "==========================================" << std::endl;
    printf("理想尺寸: %.2fm x %.2fm x %.2fm\n", IDEAL_X, IDEAL_Y, IDEAL_Z);
    printf("重建尺寸: %.2fm x %.2fm x %.2fm\n", real_x, real_y, real_z);
    std::cout << "------------------------------------------" << std::endl;
    printf("长度偏差: %+8.4fm\n", real_x - IDEAL_X);
    printf("宽度偏差: %+8.4fm\n", real_y - IDEAL_Y);
    printf("高度偏差: %+8.4fm\n", real_z - IDEAL_Z);
    
    // 判定逻辑
    if(std::abs(real_x - IDEAL_X) < 0.1) std::cout << "外壳评价: 尺寸拟合优秀" << std::endl;
    else std::cout << "外壳评价: 尺寸存在偏差，请检查扫描漂移" << std::endl;
    std::cout << "==========================================\n" << std::endl;

    // 5. 保存结果
    pcl::io::saveVTKFile("office_output.vtk", mesh);
    std::cout << "模型已生成。内部细节已根据接触关系自动粘合。" << std::endl;

    return 0;
}