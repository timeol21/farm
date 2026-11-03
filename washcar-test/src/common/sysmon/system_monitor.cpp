#include "common/sysmon/system_monitor.h"

#include "common/config/config_manager.h"
#include "common/log/log.h"
#include "common/log/log_manager.h"
#include "common/log/log_queue.h"
#include "common/log/log_sink.h"
#include "common/log/log_system.h"
#include "common/time/time.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#if defined(__linux__)
#include <sys/statvfs.h>
#endif

namespace {

// 专用 logger 名字。指标只投这里，绝不进 "work"（业务日志）。
constexpr const char* kSysLoggerName = "sysmon";

// 采样周期下限。再快就是拿 /proc 当压力测试，没意义。
constexpr int kMinIntervalMs = 1000;

// 分片睡眠的粒度：只影响 stop() 的响应延迟，不影响采样周期。
constexpr int kStopSliceMs = 200;

std::string f1(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << v;
    return oss.str();
}

#if defined(__linux__)

// ---------------- /proc & /sys 读取 ----------------

struct CpuTimes {
    unsigned long long total{0};
    unsigned long long idle{0};
};

/// /proc/stat 第一行 "cpu  user nice system idle iowait irq softirq steal ..."
bool readCpuTimes(CpuTimes& out) {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) {
        return false;
    }
    std::string tag;
    if (!(f >> tag) || tag != "cpu") {
        return false;
    }

    unsigned long long v[10] = {0};
    int n = 0;
    for (; n < 10; ++n) {
        if (!(f >> v[n])) {
            break;
        }
    }
    if (n < 4) {
        return false;
    }

    // guest / guest_nice 已经算进 user / nice 里了，再加会重复计入
    const int count = (n >= 8) ? 8 : n;
    unsigned long long total = 0;
    for (int i = 0; i < count; ++i) {
        total += v[i];
    }
    out.total = total;
    out.idle = v[3] + ((n > 4) ? v[4] : 0ULL);   // idle + iowait
    return total > 0;
}

struct MemInfo {
    unsigned long long totalKb{0};
    unsigned long long availKb{0};
    unsigned long long freeKb{0};
    unsigned long long swapTotalKb{0};
    unsigned long long swapFreeKb{0};
    bool hasAvail{false};
};

bool readMemInfo(MemInfo& out) {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        unsigned long long kb = 0;
        if (std::sscanf(line.c_str(), "MemTotal: %llu", &kb) == 1) {
            out.totalKb = kb;
        } else if (std::sscanf(line.c_str(), "MemAvailable: %llu", &kb) == 1) {
            out.availKb = kb;
            out.hasAvail = true;
        } else if (std::sscanf(line.c_str(), "MemFree: %llu", &kb) == 1) {
            out.freeKb = kb;
        } else if (std::sscanf(line.c_str(), "SwapTotal: %llu", &kb) == 1) {
            out.swapTotalKb = kb;
        } else if (std::sscanf(line.c_str(), "SwapFree: %llu", &kb) == 1) {
            out.swapFreeKb = kb;
        }
    }
    if (!out.hasAvail) {
        out.availKb = out.freeKb;   // 老内核没有 MemAvailable
    }
    return out.totalKb > 0;
}

bool readLoadAvg(double& l1, double& l5, double& l15) {
    std::ifstream f("/proc/loadavg");
    if (!f.is_open()) {
        return false;
    }
    return static_cast<bool>(f >> l1 >> l5 >> l15);
}

bool readUptimeSeconds(double& seconds) {
    std::ifstream f("/proc/uptime");
    if (!f.is_open()) {
        return false;
    }
    return static_cast<bool>(f >> seconds);
}

/// RK3568 的 SoC 温度在 thermal_zoneN，单位毫摄氏度。
/// 不同内核 zone 顺序不一样（有的是 GPU），所以取值合理就采用。
bool readCpuTemp(double& celsius) {
    for (int i = 0; i < 6; ++i) {
        const std::string path =
            "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream f(path);
        long milli = 0;
        if (!(f >> milli)) {
            continue;
        }
        const double c = static_cast<double>(milli) / 1000.0;
        if (c < -40.0 || c > 150.0) {
            continue;   // 量纲异常的节点，跳过
        }
        celsius = c;
        return true;
    }
    return false;
}

bool readCpuFreqMhz(int& mhz) {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    long khz = 0;
    if (!(f >> khz) || khz <= 0) {
        return false;
    }
    mhz = static_cast<int>(khz / 1000);
    return mhz > 0;
}

bool readSelfStatus(unsigned long long& rssKb, int& threads) {
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) {
        return false;
    }
    bool gotRss = false;
    bool gotThreads = false;
    std::string line;
    while (std::getline(f, line)) {
        unsigned long long v = 0;
        if (std::sscanf(line.c_str(), "VmRSS: %llu", &v) == 1) {
            rssKb = v;
            gotRss = true;
        } else if (std::sscanf(line.c_str(), "Threads: %llu", &v) == 1) {
            threads = static_cast<int>(v);
            gotThreads = true;
        }
        if (gotRss && gotThreads) {
            break;
        }
    }
    return gotRss;
}

bool readDiskUsage(const std::string& path, unsigned long long& totalBytes,
                   unsigned long long& availBytes) {
    struct statvfs st {};
    if (statvfs(path.c_str(), &st) != 0) {
        return false;
    }
    totalBytes = static_cast<unsigned long long>(st.f_blocks) * st.f_frsize;
    availBytes = static_cast<unsigned long long>(st.f_bavail) * st.f_frsize;
    return totalBytes > 0;
}

#endif  // __linux__

}  // namespace

SystemMonitor& SystemMonitor::instance() {
    static SystemMonitor m;
    return m;
}

SystemMonitor::~SystemMonitor() {
    stop();
}

bool SystemMonitor::init() {
    if (logger_) {
        return true;   // 已 init
    }

    const auto& cfg = ConfigManager::instance();
    const auto& sc = cfg.sysmon();

    enabled_ = sc.enabled;
    intervalMs_ = sc.intervalMs > 0 ? sc.intervalMs : 5000;
    if (intervalMs_ < kMinIntervalMs) {
        intervalMs_ = kMinIntervalMs;
    }
    logDir_ = LogSystem::dir();

    if (!enabled_) {
        LOG_INFO("[SysMon] disabled by config (sysmon.enabled=false)");
        return true;
    }
    if (logDir_.empty()) {
        LOG_WARNING("[SysMon] log dir is empty, system metrics disabled");
        return false;
    }

    // 独立队列 + 独立线程 + 独立文件。指标写慢了不会拖累业务日志。
    const std::size_t cap = cfg.queueCapacity().logCapacity > 0
                                ? static_cast<std::size_t>(cfg.queueCapacity().logCapacity)
                                : 4096;
    auto queue = std::make_shared<BlockingQueue>(cap);
    auto logger = std::make_shared<AsyncLogger>(queue);

    // prefix="sys_"  → sys_YYYY-MM-DD.txt，与业务日志 log_*.txt 分开
    // plain=true     → 只写 "[时间] 内容"，不写级别/线程号，一行一条指标
    logger->addSink(std::make_shared<FileSink>(logDir_, cfg.box().logKeepDays, "sys_", true));
    logger->setLogLevel(LogLevel::INFO);   // 指标恒为 INFO；不注册 ConsoleSink，避免 5s 刷一次终端

    logger_ = logger;
    LoggerManager::instance().registerLogger(kSysLoggerName, logger_);

    // 文件是新建的（不存在或 0 字节）就先写一行字段说明，现场拿到文件不用翻文档。
    // 放在 init 里是安全的：logger 还没 start，消息先进队列，start 后按序刷出，
    // 一定排在第一条采样之前，不会插在中间。
    const std::filesystem::path todayFile =
        std::filesystem::path(logDir_) / ("sys_" + washcar::timeutil::todayDate() + ".txt");
    std::error_code ec;
    const bool needHeader = !std::filesystem::exists(todayFile, ec) ||
                            std::filesystem::file_size(todayFile, ec) == 0;
    if (needHeader) {
        LoggerManager::instance().log(
            kSysLoggerName, LogLevel::INFO,
            "# sysmon: cpu=CPU使用率 load1/5/15=平均负载 "
            "mem_used/total/pct/avail=内存 swap_used/total=交换区 "
            "temp=SoC温度 freq=CPU频率 rss=本进程常驻内存 threads=本进程线程数 "
            "disk_free/total/used_pct=日志分区 uptime=开机时长; na=该平台取不到");
    }
    return true;
}

bool SystemMonitor::start() {
    if (!enabled_) {
        return false;
    }
    if (!logger_) {
        return false;
    }
    if (running_.load()) {
        return true;
    }
    if (!logger_->start()) {
        LOG_ERROR("[SysMon] logger start failed, system metrics disabled");
        return false;
    }

#if defined(__linux__)
    platformSupported_ = true;
    // CPU 使用率是两次累计值的差，所以必须先取一次基线。
    // 基线取完不立刻写第一条——那一条的 cpu 只覆盖几毫秒，会显示成 0%，误导排查。
    CpuTimes base;
    if (readCpuTimes(base)) {
        prevCpuTotal_ = base.total;
        prevCpuIdle_ = base.idle;
        hasPrevCpu_ = true;
    }
#else
    platformSupported_ = false;
#endif

    running_.store(true);
    // 线程从 common 的统一入口取（有全局上限保护）
    if (!thread_.start("sysmon", [this]() { loop(); })) {
        LOG_ERROR("[SysMon] 线程预算不足，采样未启动");
        running_.store(false);
        logger_->stop();
        return false;
    }

    std::cout << ">>> 系统状态监控: 每 " << intervalMs_ << "ms 一次, 文件 "
              << logDir_ << "/sys_YYYY-MM-DD.txt" << std::endl;
    LOG_INFO("[SysMon] started, intervalMs=" + std::to_string(intervalMs_)
             + ", file=" + logDir_ + "/sys_<date>.txt");
    return true;
}

void SystemMonitor::stop() {
    const bool wasRunning = running_.exchange(false);
    if (wasRunning && thread_.joinable()) {
        thread_.join();
    }
    if (logger_) {
        // 先停采集线程再停 logger：保证线程不会再往队列里 push
        logger_->stop();
        if (wasRunning) {
            LOG_INFO("[SysMon] stopped");
        }
    }
}

void SystemMonitor::loop() {
    using namespace std::chrono;

    auto next = steady_clock::now() + milliseconds(intervalMs_);

    while (running_.load()) {
        // 分片睡眠：采样周期仍是 intervalMs，但 stop() 最多等 kStopSliceMs。
        // 直接 sleep_for(intervalMs) 的话，退出要卡满一个周期（5 秒）。
        std::this_thread::sleep_for(milliseconds(kStopSliceMs));
        if (!running_.load()) {
            break;
        }
        const auto now = steady_clock::now();
        if (now < next) {
            continue;
        }

        collectAndWrite();

        next += milliseconds(intervalMs_);
        if (next <= now) {
            next = now + milliseconds(intervalMs_);   // 超时了不补拍，防堆积
        }
    }
}

void SystemMonitor::collectAndWrite() {
    const std::string line = sampleNow();
    if (line.empty()) {
        return;
    }
    LoggerManager::instance().log(kSysLoggerName, LogLevel::INFO, line);
}

std::string SystemMonitor::sampleNow() {
    if (!platformSupported_) {
        // 非 Linux：写一行说明后停止采样，避免每 5 秒刷同一条
        running_.store(false);
        return "sysmon=unsupported platform, no metrics";
    }

#if !defined(__linux__)
    return "sysmon=unsupported platform, no metrics";
#else
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    auto kv = [&oss](const char* key, bool ok, const std::string& value) {
        oss << ' ' << key << '=' << (ok ? value : std::string("na"));
    };

    // ---- CPU 使用率：与上一次采样求差 ----
    bool cpuOk = false;
    double cpuPct = 0.0;
    CpuTimes now;
    if (readCpuTimes(now)) {
        if (hasPrevCpu_ && now.total > prevCpuTotal_) {
            const unsigned long long dTotal = now.total - prevCpuTotal_;
            const unsigned long long dIdle = now.idle - prevCpuIdle_;
            cpuPct = 100.0 * (1.0 - static_cast<double>(dIdle) / static_cast<double>(dTotal));
            if (cpuPct < 0.0) {
                cpuPct = 0.0;
            }
            if (cpuPct > 100.0) {
                cpuPct = 100.0;
            }
            cpuOk = true;
        }
        prevCpuTotal_ = now.total;
        prevCpuIdle_ = now.idle;
        hasPrevCpu_ = true;
    }
    kv("cpu", cpuOk, f1(cpuPct) + "%");

    // ---- 负载 ----
    double l1 = 0.0, l5 = 0.0, l15 = 0.0;
    const bool loadOk = readLoadAvg(l1, l5, l15);
    kv("load1", loadOk, f1(l1));
    kv("load5", loadOk, f1(l5));
    kv("load15", loadOk, f1(l15));

    // ---- 内存 ----
    MemInfo mem;
    const bool memOk = readMemInfo(mem);
    const unsigned long long usedKb = memOk ? (mem.totalKb - mem.availKb) : 0;
    const double memPct = (memOk && mem.totalKb > 0)
                              ? 100.0 * static_cast<double>(usedKb) / static_cast<double>(mem.totalKb)
                              : 0.0;
    kv("mem_used", memOk, std::to_string(usedKb / 1024) + "MB");
    kv("mem_total", memOk, std::to_string(mem.totalKb / 1024) + "MB");
    kv("mem_pct", memOk, f1(memPct) + "%");
    kv("mem_avail", memOk, std::to_string(mem.availKb / 1024) + "MB");

    const bool swapOk = memOk && mem.swapTotalKb > 0;
    kv("swap_used", swapOk,
       std::to_string((mem.swapTotalKb - mem.swapFreeKb) / 1024) + "MB");
    kv("swap_total", swapOk, std::to_string(mem.swapTotalKb / 1024) + "MB");

    // ---- 温度 / 频率 ----
    // 注意：必须先读进局部变量再拼字符串。
    // 写成 kv("temp", readCpuTemp(t), f1(t)+"C") 是错的——C++ 实参求值顺序未定义，
    // f1(t) 可能先于 readCpuTemp(t) 求值，拿到旧值 0.0（早期版本就是这么写出 uptime=0s 的）。
    double tempC = 0.0;
    const bool tempOk = readCpuTemp(tempC);
    kv("temp", tempOk, f1(tempC) + "C");

    int freqMhz = 0;
    const bool freqOk = readCpuFreqMhz(freqMhz);
    kv("freq", freqOk, std::to_string(freqMhz) + "MHz");

    // ---- 本进程自身 ----
    unsigned long long rssKb = 0;
    int threads = 0;
    const bool selfOk = readSelfStatus(rssKb, threads);
    kv("rss", selfOk, f1(static_cast<double>(rssKb) / 1024.0) + "MB");
    kv("threads", selfOk, std::to_string(threads));

    // ---- 日志所在分区：Flash 写满是最常见的"跑几天就挂"原因 ----
    unsigned long long diskTotal = 0;
    unsigned long long diskAvail = 0;
    const bool diskOk = !logDir_.empty() && readDiskUsage(logDir_, diskTotal, diskAvail);
    const double kbPerGb = 1024.0 * 1024.0;
    kv("disk_free", diskOk, f1(static_cast<double>(diskAvail) / kbPerGb) + "GB");
    kv("disk_total", diskOk, f1(static_cast<double>(diskTotal) / kbPerGb) + "GB");
    const double diskUsedPct =
        (diskOk && diskTotal > 0)
            ? 100.0 * (1.0 - static_cast<double>(diskAvail) / static_cast<double>(diskTotal))
            : 0.0;
    kv("disk_used_pct", diskOk, f1(diskUsedPct) + "%");

    // ---- 开机时长 ----
    double uptimeSec = 0.0;
    const bool uptimeOk = readUptimeSeconds(uptimeSec);
    kv("uptime", uptimeOk,
       std::to_string(static_cast<long long>(uptimeSec)) + "s");

    // 去掉第一个 kv 前多余的空格
    std::string line = oss.str();
    if (!line.empty() && line.front() == ' ') {
        line.erase(line.begin());
    }
    return line;
#endif
}
