#include "common/log/log_sink.h"
#include <chrono>
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
    // m_basePath 是目录,不是文件名前缀。拼成 data/log_*.txt 会让日志落到 storageRoot 根上,
    // cleanupOldLogs 扫目录时一条也看不到 —— 目录就是目录,只在这里定义一次。
    return (fs::path(m_basePath) / (m_prefix + date + ".txt")).string();
}

FileSink::FileSink(const std::string& basePath, int keepDays,
                   const std::string& prefix, bool plain)
    : m_basePath(basePath)
    , m_prefix(prefix.empty() ? std::string("log_") : prefix)
    , m_plain(plain)
    , m_keepDays(keepDays > 0 ? keepDays : 7)
{
    // 日志目录由 FileSink 自己建:调用方只给路径,建目录这件事只有这一处
    std::error_code ec;
    fs::create_directories(m_basePath, ec);
    // 开机清一次。只在跨零点切文件时清的话,盒子连开几天(现场就是这个状态)过期日志一条也删不掉。
    cleanupOldLogs();

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

    std::ostringstream oss;
    if (m_plain) {
        // 指标文件:一行一条,只有时间和内容。带 [T:线程号] 只会干扰 grep 和画图。
        oss << "[" << formatTime(msg.timestamp) << "] "
            << msg.message << "\n";
    } else {
        // 2️⃣ 格式化日志
        oss << "[" << formatTime(msg.timestamp) << "]"
            << " [" << levelToString(msg.level) << "]"
            << " [T:" << std::hash<std::thread::id>{}(msg.threadId) << "]";
        if (!msg.module.empty()) {
            oss << " [" << msg.module << "]";
        }
        oss << " " << msg.message << "\n";
    }

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
    cleanupOldLogs();
}

// 每天切文件时顺带删掉过期日志。现场 PLC 断线时一天能刷 80MB,
// 不设上限的话一个月就是 2G 多,把盒子写满。
void FileSink::cleanupOldLogs() const
{
    std::error_code ec;
    if (!fs::exists(m_basePath, ec)) {
        return;
    }

    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * m_keepDays);
    for (const auto& entry : fs::directory_iterator(m_basePath, ec)) {
        const std::string name = entry.path().filename().string();
        // 只删自己这个前缀的 *_YYYY-MM-DD.txt,目录里别人的文件不碰。
        // 业务日志用 "log_",系统指标用 "sys_",两者互不越界。
        if (name.rfind(m_prefix, 0) != 0 || entry.path().extension() != ".txt") {
            continue;
        }
        if (fs::last_write_time(entry.path(), ec) < cutoff) {
            std::error_code rmEc;
            if (fs::remove(entry.path(), rmEc)) {
                std::cerr << "[FileSink] removed expired log: " << name << std::endl;
            }
        }
    }
}

void ConsoleSink::write(const LogMessage& msg)
{
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &msg.timestamp);
#else
    localtime_r(&msg.timestamp, &tm);
#endif
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);

    const char* levelStr = "UNKNOWN";
    switch (msg.level) {
        case LogLevel::DEBUG:   levelStr = "DEBUG"; break;
        case LogLevel::INFO:    levelStr = "INFO"; break;
        case LogLevel::WARNING: levelStr = "WARNING"; break;
        case LogLevel::ERROR:   levelStr = "ERROR"; break;
    }

    std::cout << "[" << timeBuf << "]"
              << " [" << levelStr << "]"
              << " " << msg.message << std::endl;
}
