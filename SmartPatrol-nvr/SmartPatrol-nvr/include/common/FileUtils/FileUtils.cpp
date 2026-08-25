#include "FileUtils.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace fs = std::filesystem;

struct VideoFileInfoCheck {
    fs::path path;
    fs::file_time_type time;
};


std::string FileUtils::generateFileName(
    const std::string& cameraDir,
    const std::string& ext)
{
    // 2. 获取当前系统时间（本地时间）
    std::time_t now = std::time(nullptr);
    std::tm* tmNow = std::localtime(&now);
    if (!tmNow) {
        throw std::runtime_error("Failed to get local time");
    }

    // 3. 拼接完整目录路径：cameraDir + / + 日期目录（如 /data/videos/摄像头01/20251116）
    std::stringstream dirSs;
    dirSs << cameraDir << "/"
          << std::put_time(tmNow, "%Y%m%d");  // 日期目录：YYYYMMDD（20251116）

    std::string fullDir = dirSs.str();
    // 4. 确保多级目录存在（cameraDir 若不存在也会自动创建，兜底）
    if (!ensureDirExists(fullDir)) {
        throw std::runtime_error("Failed to create directory: " + fullDir);
    }

    // 5. 拼接最终文件路径：完整目录/具体时间.mp4（如 143025.mp4）
    std::stringstream fileSs;
    fileSs << fullDir << "/"
           << std::put_time(tmNow, "%H_%M")  // 文件名仅保留具体时间（HHMMSS）
           << "." << ext;

    return fileSs.str();
}


bool FileUtils::ensureDirExists(const std::string& path)
{
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
        return true;
    } catch (...) {
        return false;
    }
}

static bool isVideoFile(const fs::path& p,const std::vector<std::string>& exts) {
    if (!fs::is_regular_file(p)) {
        return false;
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (auto& e : exts) {
        std::string le = e;
        std::transform(le.begin(), le.end(), le.begin(), ::tolower);
        if (ext == le) {
            return true;
        }
    }
    return false;
}

bool FileUtils::keepLatestFiles(const std::string& dirPath,size_t maxFiles,const std::vector<std::string>& videoExts) {

    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    // 新增：打印关键参数，排查问题
    std::cout << "[Debug] maxFiles: " << maxFiles << std::endl;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        std::cerr << "[VideoFileCleaner] not a directory: "
                  << dirPath << std::endl;
        return false;
    }
 

    std::vector<VideoFileInfoCheck> videos;
     videos.reserve(16);

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        const auto& p = entry.path();
        if (isVideoFile(p, videoExts)) {
            videos.push_back({p, fs::last_write_time(p)});
        }
    }
    // 新增：打印实际筛选出的视频文件数量
    std::cout << "[Debug] 筛选出的视频文件数量: " << videos.size() << std::endl;

    if (videos.size() <= maxFiles) {
        return true; // 不需要删除
    }

    // 按时间从旧到新排序
    std::sort(videos.begin(), videos.end(),
        [](const VideoFileInfoCheck& a, const VideoFileInfoCheck& b) {
            return a.time < b.time;
        });

    size_t needDelete = videos.size() - maxFiles;
    for (size_t i = 0; i < needDelete; ++i) {
        try {
            fs::remove(videos[i].path);
            std::cout << "[VideoFileCleaner] removed: "
                      << videos[i].path << std::endl;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[VideoFileCleaner] remove failed: "
                      << e.what() << std::endl;
        }
    }

    return true;
}


bool FileUtils::fileExists(const std::string& filePath)
{
    struct stat st;
    if (stat(filePath.c_str(), &st) != 0) {
        return false; // 不存在 or 无权限
    }

    // 必须是普通文件
    return S_ISREG(st.st_mode);
}
