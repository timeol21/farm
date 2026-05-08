#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>

#include <fstream>
#include <iomanip>

static int file_index = 0;

void cloudCallback(const sensor_msgs::PointCloud2ConstPtr& msg)
{
    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::fromROSMsg(*msg, cloud);

    if (cloud.empty())
    {
        ROS_WARN("Received empty point cloud");
        return;
    }

    std::ostringstream filename;
    filename << "/home/ztl/pointcloud_txt/cloud_"
             << std::setw(4) << std::setfill('0') << file_index++
             << ".txt";

    std::ofstream ofs(filename.str());
    if (!ofs.is_open())
    {
        ROS_ERROR("Failed to open file: %s", filename.str().c_str());
        return;
    }

    for (const auto& p : cloud.points)
    {
        ofs << p.x << " " << p.y << " " << p.z << "\n";
    }

    ofs.close();

    ROS_INFO("Saved %lu points to %s",
             cloud.points.size(),
             filename.str().c_str());
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "pcl_to_txt_node");
    ros::NodeHandle nh;

    ros::Subscriber sub = nh.subscribe(
        "/unilidar/cloud",
        1,
        cloudCallback
    );

    ROS_INFO("pcl_to_txt_node started, waiting for point cloud...");

    ros::spin();
    return 0;
}

