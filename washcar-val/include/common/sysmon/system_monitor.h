#pragma once

// =============================================================================
// SystemMonitor —— 盒子自身运行状态采样（CPU / 内存 / 温度 / 磁盘 / 自身占用）
//
// 定位：它不属于任何一层。三层关心的是"洗车机在干什么"，它关心的是
//       "盒子还活着吗、会不会卡死、会不会把 Flash 写满"。
//
// 输出：**专用日志文件** <logDir>/sys_YYYY-MM-DD.txt，与业务日志 log_*.txt 分开。
//   业务日志按事件写（有命令才有记录，闲时是空的），指标按固定节拍写；
//   混在一个文件里既不好 grep，也没法画曲线。
//
//   init()  —— 建专用 logger + FileSink(prefix="sys_", plain=true)，不开线程
//   start() —— 采一次 CPU 基线，然后起采样线程；此后每 intervalMs 写一行
//   stop()  —— 停线程、停 logger（flush 掉队列里剩下的行）
//
// 为什么单独一个 logger 而不是复用 "work"：
//   AsyncLogger 的 sink 列表是共享的，用 "work" 会把指标混进业务日志文件。
//   独立 logger 意味着独立队列 + 独立线程 + 独立文件，互不影响；
//   指标写慢了不会拖累业务日志，反之亦然。
//
// 采样内容（每行一条，key=value，取不到的写 na）：
//   cpu=12.3%          两次采样之间的 CPU 使用率（/proc/stat 差分）
//   load1/load5/load15 平均负载（/proc/loadavg）
//   mem_used/mem_total/mem_pct/mem_avail   内存（/proc/meminfo，用 MemAvailable 算已用）
//   swap_used/swap_total                   交换区
//   temp=48.5C         SoC 温度（/sys/class/thermal/thermal_zoneN/temp，RK3568 有）
//   freq=1608MHz       CPU 当前频率（cpufreq scaling_cur_freq）
//   rss=4.2MB          本进程常驻内存（/proc/self/status VmRSS）
//   threads=12         本进程线程数
//   disk_free/disk_total/disk_pct          日志所在分区剩余（statvfs）
//   uptime=18305s      开机时长（/proc/uptime）
//
// 平台：仅 Linux 有真实数据。其他平台写一行 sysmon=unsupported 后不再采样，
//       保证工程仍能编译（本项目的其他部分本来就依赖 termios，实际只跑 Linux）。
// =============================================================================

#include "common/thread/thread_manager.h"

#include <atomic>
#include <memory>
#include <string>

class AsyncLogger;

class SystemMonitor {
public:
    static SystemMonitor& instance();

    /// 按配置装配专用 logger 与文件 sink。失败返回 false（调用方决定是否致命，
    /// 通常只告警——监控挂了不该让洗车机起不来）。
    bool init();

    /// 采一次 CPU 基线并起采样线程。未 init 或配置关闭时返回 false。
    bool start();

    /// 停采样线程，再停专用 logger（join + flush 队列剩余行）。
    void stop();

    bool enabled() const { return enabled_; }
    int  intervalMs() const { return intervalMs_; }
    /// 专用日志文件所在的目录（空串 = 未 init）
    const std::string& logDir() const { return logDir_; }

    /// 立即采一次并返回格式化后的整行（不落日志）。自测/排查用。
    std::string sampleNow();

private:
    SystemMonitor() = default;
    ~SystemMonitor();
    SystemMonitor(const SystemMonitor&) = delete;
    SystemMonitor& operator=(const SystemMonitor&) = delete;

    void loop();
    void collectAndWrite();

    std::shared_ptr<AsyncLogger> logger_;
    ManagedThread thread_;
    std::atomic<bool> running_{false};

    bool enabled_{false};
    int  intervalMs_{5000};
    std::string logDir_;

    // CPU 使用率必须靠两次累计值求差，单次读 /proc/stat 只能得到开机以来的平均值
    unsigned long long prevCpuTotal_{0};
    unsigned long long prevCpuIdle_{0};
    bool hasPrevCpu_{false};

    bool platformSupported_{true};
};
