#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/features/normal_3d.h>
#include <pcl/surface/gp3.h>
#include <pcl/io/vtk_io.h>
#include <pcl/search/kdtree.h> // 显式包含搜索树头文件

int main(int argc, char** argv) {
    // 1. 加载点云
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
    if (pcl::io::loadPCDFile<pcl::PointXYZ>("../scans.pcd", *cloud) == -1) {
        PCL_ERROR("无法读取文件 scans.pcd \n");
        return (-1);
    }

    // 2. 估计法向量
    pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> n;
    pcl::PointCloud<pcl::Normal>::Ptr normals (new pcl::PointCloud<pcl::Normal>);
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
    tree->setInputCloud (cloud);
    n.setInputCloud (cloud);
    n.setSearchMethod (tree);
    n.setKSearch (20);
    n.compute (*normals);

    // 3. 将点云和法向量合并 (关键修正：GP3需要处理 PointNormal)
    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals (new pcl::PointCloud<pcl::PointNormal>);
    pcl::concatenateFields (*cloud, *normals, *cloud_with_normals);

    // 4. 为重建算法创建专用的搜索树 (必须是 PointNormal 类型)
    pcl::search::KdTree<pcl::PointNormal>::Ptr tree2 (new pcl::search::KdTree<pcl::PointNormal>);
    tree2->setInputCloud (cloud_with_normals);

    // 5. 设置贪婪三角化参数
    pcl::GreedyProjectionTriangulation<pcl::PointNormal> gp3;
    pcl::PolygonMesh triangles;

    gp3.setSearchRadius (0.1);           // 搜索半径，根据你的场景（15米）可以调大到 0.2
    gp3.setMu (2.5);                     // 密度影响因子
    gp3.setMaximumNearestNeighbors (100);
    gp3.setMaximumSurfaceAngle(M_PI/4); // 45度
    gp3.setMinimumAngle(M_PI/18);       // 10度
    gp3.setMaximumAngle(2*M_PI/3);      // 120度
    gp3.setNormalConsistency(false);

    // 6. 执行重建
    gp3.setInputCloud (cloud_with_normals);
    gp3.setSearchMethod (tree2);         // 使用修正后的 tree2
    gp3.reconstruct (triangles);

    // 7. 保存结果
    pcl::io::saveVTKFile ("room_model.vtk", triangles);
    std::cout << "3D模型已生成：room_model.vtk" << std::endl;

    return 0;
}