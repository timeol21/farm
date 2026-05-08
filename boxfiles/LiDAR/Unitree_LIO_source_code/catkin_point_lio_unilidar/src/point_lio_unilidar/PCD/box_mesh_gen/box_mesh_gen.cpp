#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/features/normal_3d.h>
#include <pcl/surface/gp3.h>
#include <pcl/io/vtk_io.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>

int main(int argc, char** argv) {
    // 1. 加载点云
    pcl::PointCloud<pcl::PointXYZI>::Ptr cloud_i(new pcl::PointCloud<pcl::PointXYZI>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    
    // 请确保路径正确，或直接使用绝对路径
    if (pcl::io::loadPCDFile<pcl::PointXYZI>("../scans.pcd", *cloud_i) == -1) {
        std::cerr << "无法读取 scans.pcd 文件，请检查路径！" << std::endl;
        return -1;
    }
    pcl::copyPointCloud(*cloud_i, *cloud);
    std::cout << "原始点数: " << cloud->size() << std::endl;

    // 2. 预处理 A：下采样（让点分布均匀，生成的面更平整）
    pcl::VoxelGrid<pcl::PointXYZ> vg;
    vg.setInputCloud(cloud);
    vg.setLeafSize(0.02f, 0.02f, 0.02f); // 2cm 聚合，可根据精度调整
    vg.filter(*cloud);

    // 3. 预处理 B：去噪（防止噪点导致错误的“粘连”）
    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
    sor.setInputCloud(cloud);
    sor.setMeanK(50);
    sor.setStddevMulThresh(1.0);
    sor.filter(*cloud);

    // 4. 计算法向量（建模的关键）
    pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> n;
    pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    tree->setInputCloud(cloud);
    n.setInputCloud(cloud);
    n.setSearchMethod(tree);
    n.setKSearch(20);
    n.compute(*normals);

    // 5. 合并点与法向量
    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals(new pcl::PointCloud<pcl::PointNormal>);
    pcl::concatenateFields(*cloud, *normals, *cloud_with_normals);

    // 6. 贪婪三角化重建
    pcl::search::KdTree<pcl::PointNormal>::Ptr tree2(new pcl::search::KdTree<pcl::PointNormal>);
    tree2->setInputCloud(cloud_with_normals);

    pcl::GreedyProjectionTriangulation<pcl::PointNormal> gp3;
    pcl::PolygonMesh triangles;

    // --- 核心参数调整 ---
    // 这个半径决定了“粘合”距离。接触物体的点距通常小于此值，不接触的通常大于此值。
    gp3.setSearchRadius(0.3);              // 设为 30cm，你可以根据物体实际间距调整 (0.1 - 0.5)
    gp3.setMu(2.5);                        // 表面密度响应因子
    gp3.setMaximumNearestNeighbors(100); 
    gp3.setMaximumSurfaceAngle(M_PI/2);    // 90度，允许长方体直角拐角连在一起
    gp3.setMinimumAngle(M_PI/18);          // 10度
    gp3.setMaximumAngle(2*M_PI/3);         // 120度
    gp3.setNormalConsistency(true);        // 保持法线一致，区分内外表面

    gp3.setInputCloud(cloud_with_normals);
    gp3.setSearchMethod(tree2);
    gp3.reconstruct(triangles);

    // 7. 保存为 VTK 格式
    pcl::io::saveVTKFile("box_connected_model.vtk", triangles);
    std::cout << "建模完成！生成文件: box_connected_model.vtk" << std::endl;

    return 0;
}