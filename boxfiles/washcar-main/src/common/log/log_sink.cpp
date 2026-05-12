#include "common/log/log_sink.h"
namespace fs = std::filesystem;

static std::string levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

static std::string getCurrentDate() {
    std::time_t t = std::time(nullptr);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf);
}
static std::string formatTime(std::time_t t)
{
    std::tm tm{};

#ifdef _WIN32
    // localtime_s(&tm, &t);   // Windows 线程安
#else
    localtime_r(&t, &tm);   // Linux 线程安全
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

void cleanupOldLogs()//不用先实现，后续实现
{
    // 遍历目录
    // 找到 log_YYYY-MM-DD.txt
    // 解析日期
    // 删除超过7天的文件
}

std::string FileSink::buildFileName(const std::string& date) {
    return m_basePath + "_" + date + ".txt";
}

FileSink::FileSink(const std::string& basePath)
    : m_basePath(basePath)
{
    m_currentDate = getCurrentDate();

    std::string fileName = buildFileName(m_currentDate);

    m_file.open(fileName, std::ios::app | std::ios::out);
    m_file.setf(std::ios::unitbuf);
    if (!m_file.is_open()) {
        std::cerr << "Failed to open log file: " << fileName << std::endl;
    }

}

void FileSink::write(const LogMessage& msg)
{
    // std::lock_guard<std::mutex> lock(mtx);    
    rotateIfNeeded();

    if (!m_file.is_open()) return;
    // 2️⃣ 格式化日志
    std::ostringstream oss;
    oss << "[" << formatTime(msg.timestamp) << "]"
        << " [" << levelToString(msg.level) << "]"
        << " [T:" << std::hash<std::thread::id>{}(msg.threadId) << "]";
    if (!msg.module.empty()) {
        oss << " [" << msg.module << "]";
    }
    oss << " " << msg.message << "\n";

    // 3️⃣ 写入
    m_file << oss.str();
    // m_file.flush();
    // 4️⃣ 可选：ERROR强制flush
    if (msg.level == LogLevel::ERROR) {
        m_file.flush();
    }
}
void FileSink::rotateIfNeeded()
{
    std::string today = getCurrentDate();

    if (today == m_currentDate) {
        return;
    }

    // 日期变化
    m_currentDate = today;

    if (m_file.is_open()) {
        m_file.close();
    }

    std::string newFile = buildFileName(today);

    // 创建目录
    fs::create_directories(fs::path(newFile).parent_path());

    m_file.open(newFile, std::ios::app);

    if (!m_file.is_open()) {
        std::cerr << "Failed to open log file: " << newFile << std::endl;
    }

    // 可选：清理旧日志
    // cleanupOldLogs();
}
