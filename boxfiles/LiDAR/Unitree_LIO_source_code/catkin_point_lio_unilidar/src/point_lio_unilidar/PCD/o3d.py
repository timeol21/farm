import open3d as o3d
import numpy as np

# 1. 加载你的 TXT 数据 (假设只有 x, y, z)
data = np.loadtxt("final_raw_points.txt", delimiter=',', usecols=(0,1,2), skiprows=1)
pcd = o3d.geometry.PointCloud()
pcd.points = o3d.utility.Vector3dVector(data)

# 2. 必须先计算法向量，否则没法成面
pcd.estimate_normals()

# 3. 使用滚球法 (Ball Pivoting) 生成三角面
distances = pcd.compute_nearest_neighbor_distance()
avg_dist = np.mean(distances)
radius = 3 * avg_dist
mesh = o3d.geometry.TriangleMesh.create_from_point_cloud_ball_pivoting(
    pcd, o3d.utility.DoubleVector([radius, radius * 2]))

# 4. 可视化
o3d.visualization.draw_geometries([mesh])