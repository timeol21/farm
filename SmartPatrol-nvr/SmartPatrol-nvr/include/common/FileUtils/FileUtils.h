#ifndef FILE_UTILS_H
#define FILE_UTILS_H
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstdio> 
#include <sys/stat.h> 
#include <unistd.h>
#include <mutex>
class FileUtils {
public:
    /**
     * 生成格式：cameraDir（绝对路径）/日期目录/具体时间.mp4（例：/data/videos/摄像头01/20251116/143025.mp4）
     * @param cameraDir 摄像头绝对路径（已包含摄像头名，如 "/data/videos/摄像头01"）
     * @param ext 文件后缀（默认 mp4）
     * @return 完整的文件路径
     * @throw std::runtime_error 时间获取失败或目录创建失败时抛出
     */
    static std::string generateFileName(
        const std::string& cameraDir,
        const std::string& ext = "mp4");
    // 检查并创建目录
    static bool ensureDirExists(const std::string& path);

    // 保留最多 maxFiles 个视频文件，超出则删除最旧的
    // videoExts 例如: {".mp4", ".avi", ".mkv"}
    static bool keepLatestFiles(const std::string& dirPath,size_t maxFiles = 5,const std::vector<std::string>& videoExts = {".mp4"});   

    static bool fileExists(const std::string& filePath);   

    static std::mutex debugMutex;
};

#endif