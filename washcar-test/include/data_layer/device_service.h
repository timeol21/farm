#pragma once

// IDeviceService 的实现：定时器 + 点位表 + 内存缓存。
//
// ============ 数据流（唯一原则）============
//   PLC --(采集线程每 intervalMs 读一轮)--> 内存缓存 --> 业务层只读缓存
//
// **业务层永远不碰 PLC**。viewAllDeviceStatus() 只做一次 map 拷贝，
// 不发起任何串口/TCP 请求。这样：
//   1. 串口只有一个读者，不会出现"业务线程和采集线程抢串口"导致命令卡住；
//   2. 命令响应时间与 PLC 通讯质量解耦；
//   3. PLC 断线时业务层拿到的是"最后一次成功采集的快照"，而不是现读失败。
//
// 代价：读到的是最多 1 个采集周期之前的值（默认 1000ms）。
// 对洗车机这种秒级流程完全够用；真要更实时就调小 acquisition.intervalMs。
//
// ============ PLC 实例从哪来 ============
// 实例的持有与登记用主项目的 FxPlcInstanceSet（addPlc / getPlc / getAll），
// 本类不自己管 map，也不决定"有哪些 PLC"——那是 DataLayer::init 的事。
// 本类只负责：按点位表决定"读哪些点"、怎么缓存、怎么判变化。
//
// ============ 缓存策略：值没变就不写 ============
//   · 和缓存里一样      → 不写（跳过）
//   · 和缓存里不一样    → 写这一个点
//   · 本轮没读到（失败）→ 保留缓存里的旧值，不删点

#include "common/config/config_object.h"
#include "common/plc_point.h"
#include "common/thread/thread_manager.h"
#include "data_layer/fx_plc/fx_plc_instance_set.h"
#include "data_layer/idevice_service.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class DeviceService : public IDeviceService {
public:
    /// table 的生命周期需长于本对象（通常是 ConfigManager 里的那份）；
    /// set 由 DataLayer 持有并注入，约定与主项目的采集任务一致。
    DeviceService(const washcar::cfg::MachinePointTable& table, FxPlcInstanceSet& set);
    ~DeviceService() override;

    DeviceService(const DeviceService&) = delete;
    DeviceService& operator=(const DeviceService&) = delete;

    PlcPointValues viewAllDeviceStatus() override;
    bool forceFxM(const std::string& plcId, int addr, bool turnOn) override;
    bool forceFxX(const std::string& plcId, int addr, bool turnOn) override;
    std::vector<std::pair<std::string, PlcPointValues>> getChanged() override;
    void clearChanged(const std::vector<std::string>& plcIds) override;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    /// 当前采集周期（毫秒），启动时从 pointTable.acquisition.intervalMs 固化
    int intervalMs() const { return intervalMs_; }

    /// 采集统计。用来验证"值没变就不写缓存"这条策略是否真的生效：
    ///   writtenPoints 远小于 freshPoints 说明绝大多数点都没变化。
    struct Stats {
        unsigned long long polls{0};          // 成功合并数据的轮数
        unsigned long long freshPoints{0};    // 各轮读回来的点位总数
        unsigned long long writtenPoints{0};  // 其中真正写进缓存的（= 发生变化的次数）
    };

private:
    struct Entry {
        PlcPointValues points;
        bool changed{false};
    };

    void loop();

    bool poll();
    void readOne(IPlcInstance& plc, PlcPointValues& out);
    /// 把一轮结果逐点合并进缓存，返回真正写入的点位数（0 = 本轮无变化）
    std::size_t mergeIntoCache(const std::string& plcId, const PlcPointValues& fresh);
    bool snapshot(const std::string& plcId, PlcPointValues& out) const;
    std::string firstPlcId() const;

    bool writeM(const std::string& plcId, int addr, bool turnOn);
    bool writeX(const std::string& plcId, int addr, bool turnOn);

    void logReadFailure(const std::string& plcId, const std::string& point);
    void logKeyStateChanges(const PlcPointValues& points, const std::string& plcId);

    const washcar::cfg::MachinePointTable& table_;
    FxPlcInstanceSet& set_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> cache_;
    std::unordered_map<std::string, int> failCounts_;
    Stats stats_;
    // keyState 基准单独一把锁：它在 poll 的持锁段里被调用，
    // 共用 mutex_ 会自锁（std::mutex 不可重入）
    mutable std::mutex keyStateMutex_;
    std::unordered_map<std::string, PlcPointValues> keyStateBaseline_;

    ManagedThread thread_;
    std::atomic<bool> running_{false};
    int intervalMs_{500};
};
