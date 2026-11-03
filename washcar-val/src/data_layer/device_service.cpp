#include "data_layer/device_service.h"

#include "common/config/config_manager.h"
#include "common/config/point.h"
#include "common/log/log_manager.h"

#include <chrono>
#include <map>
#include <string>

namespace {

/// 采集周期下限。再快串口来不及回包，只会把失败日志刷满。
constexpr int kMinIntervalMs = 100;

using washcar::cfg::formatPoint;
using washcar::cfg::parsePoint;

/// 按组读 X / Y：一次往返回该组 8 个位，组内每个点各取一位。
/// 组号 = addr/10，位序 = addr%10（与 FxPlcInstance::readX/readY 一致）。
///
/// 组读失败时整组都算失败，交给调用方计告警——单点重读没有意义，
/// 因为失败原因是串口没回包，不是某个位不存在。
void readGroupBits(IPlcInstance& plc, char type,
                   const std::vector<std::string>& pointNames,
                   PlcPointValues& out,
                   std::vector<std::string>& failed) {
    // group -> [(规范点名, 地址)]
    std::map<int, std::vector<std::pair<std::string, int>>> groups;
    for (const auto& name : pointNames) {
        const auto parsed = parsePoint(name);
        if (parsed.type != type) {
            continue;
        }
        groups[parsed.addr / 10].push_back({formatPoint(parsed.type, parsed.addr), parsed.addr});
    }

    for (const auto& [group, members] : groups) {
        int status8 = 0;
        const bool ok = (type == 'X') ? plc.readX(group, status8) : plc.readY(group, status8);
        if (!ok) {
            for (const auto& m : members) {
                failed.push_back(m.first);
            }
            continue;
        }
        for (const auto& [name, addr] : members) {
            out.set(name, ((status8 >> (addr % 10)) & 1) != 0 ? 1 : 0);
        }
    }
}

}  // namespace

DeviceService::DeviceService(const washcar::cfg::MachinePointTable& table, FxPlcInstanceSet& set)
    : table_(table), set_(set) {
    const int configured = table_.acquisition.intervalMs;
    intervalMs_ = configured > 0 ? configured : 500;
    if (intervalMs_ < kMinIntervalMs) {
        intervalMs_ = kMinIntervalMs;
    }
}

DeviceService::~DeviceService() {
    stop();
}

// ============================ PLC 读取与缓存 ============================

bool DeviceService::poll() {
    // 先把待采的 PLC 列出来，串口/网络 IO 期间不持锁（读一轮可能要几百 ms）。
    // 集合在 init 之后不再变动，所以这里不需要额外加锁。
    std::vector<std::pair<std::string, IPlcInstance*>> targets;
    targets.reserve(set_.getAll().size());
    for (const auto& [plcId, plc] : set_.getAll()) {
        if (plc) {
            targets.emplace_back(plcId, plc.get());
        }
    }

    std::vector<std::pair<std::string, PlcPointValues>> fresh;
    fresh.reserve(targets.size());

    for (auto& [plcId, plc] : targets) {
        if (!plc->connect()) {
            LOG_WARNING("[DeviceService] PLC " + plcId + " connect failed, skip this cycle");
            continue;
        }
        PlcPointValues values;
        readOne(*plc, values);
        fresh.emplace_back(plcId, std::move(values));
    }

    if (fresh.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t totalFresh = 0;
    std::size_t totalWritten = 0;

    for (auto& [plcId, values] : fresh) {
        totalFresh += values.size();
        const std::size_t written = mergeIntoCache(plcId, values);
        if (written == 0) {
            // 整轮无变化，属于稳态（洗车机待机时几乎每轮都走这里）
            continue;
        }
        totalWritten += written;

        // 只有真的变了才动 keyState 基线，避免稳态时每轮都做一次比对
        logKeyStateChanges(cache_[plcId].points, plcId);

        // 变化明细进 DEBUG：稳态下不刷屏，排查时把 logLevel 调成 debug 就能看到
        LOG_DEBUG("[DeviceService] " + plcId + " changed points=" + std::to_string(written)
                  + "/" + std::to_string(values.size()));
    }

    stats_.polls += 1;
    stats_.freshPoints += totalFresh;
    stats_.writtenPoints += totalWritten;
    return true;
}

void DeviceService::readOne(IPlcInstance& plc, PlcPointValues& out) {
    const auto& acq = table_.acquisition;
    const std::string plcId = plc.getPlcId();

    // ---- D 寄存器：逐个读（每个 D 是独立地址，不能合） ----
    for (const auto& name : acq.dPoints) {
        const auto parsed = parsePoint(name);
        if (parsed.type != 'D') {
            continue;
        }
        uint16_t value = 0;
        if (plc.readD(parsed.addr, value)) {
            out.set(formatPoint(parsed.type, parsed.addr), static_cast<int>(value));
        } else {
            logReadFailure(plcId, name);
        }
    }

    // ---- Y / X：按组读 ----
    std::vector<std::string> failed;
    readGroupBits(plc, 'Y', acq.yPoints, out, failed);
    for (const auto& name : failed) {
        logReadFailure(plcId, name);
    }

    failed.clear();
    readGroupBits(plc, 'X', acq.xPoints, out, failed);
    for (const auto& name : failed) {
        logReadFailure(plcId, name);
    }

    // ---- M 点：逐个读 ----
    for (const auto& name : acq.mPoints) {
        const auto parsed = parsePoint(name);
        if (parsed.type != 'M') {
            continue;
        }
        bool state = false;
        if (plc.readM(parsed.addr, state)) {
            out.set(formatPoint(parsed.type, parsed.addr), state ? 1 : 0);
        } else {
            logReadFailure(plcId, name);
        }
    }

    // ---- S 点：逐个读 ----
    for (const auto& name : acq.sPoints) {
        const auto parsed = parsePoint(name);
        if (parsed.type != 'S') {
            continue;
        }
        bool state = false;
        if (plc.readS(parsed.addr, state)) {
            out.set(formatPoint(parsed.type, parsed.addr), state ? 1 : 0);
        } else {
            logReadFailure(plcId, name);
        }
    }
}

std::size_t DeviceService::mergeIntoCache(const std::string& plcId, const PlcPointValues& fresh) {
    // 注意：调用方已持 mutex_
    Entry& entry = cache_[plcId];
    const bool firstRound = entry.points.empty();

    std::size_t written = 0;
    for (const auto& [key, value] : fresh.values) {
        if (entry.points.has(key) && entry.points.at(key) == value) {
            continue;   // 和上次读到的值一样 → 不写内存
        }
        entry.points.set(key, value);
        ++written;
    }

    // 本轮没读到的点（读失败）保持旧值，不做删除：
    // 偶发一次失败就把点抹掉，会让业务层把"读失败"误当成"值为 0"。

    if (firstRound) {
        // 首轮建立基准，算一次"有数据可报"
        entry.changed = !entry.points.empty();
    } else if (written > 0) {
        entry.changed = true;
    }
    return written;
}

bool DeviceService::snapshot(const std::string& plcId, PlcPointValues& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(plcId);
    if (it == cache_.end() || it->second.points.empty()) {
        return false;
    }
    out = it->second.points;
    return true;
}

std::string DeviceService::firstPlcId() const {
    const auto& all = set_.getAll();
    return all.empty() ? std::string() : all.begin()->first;
}

bool DeviceService::writeM(const std::string& plcId, int addr, bool turnOn) {
    IPlcInstance* plc = set_.getPlc(plcId);
    if (plc == nullptr) {
        LOG_ERROR("[DeviceService] writeM: PLC not found, plcId=" + plcId
                  + ", m=" + std::to_string(addr));
        return false;
    }
    return plc->forceM(addr, turnOn);
}

bool DeviceService::writeX(const std::string& plcId, int addr, bool turnOn) {
    IPlcInstance* plc = set_.getPlc(plcId);
    if (plc == nullptr) {
        LOG_ERROR("[DeviceService] writeX: PLC not found, plcId=" + plcId
                  + ", x=" + std::to_string(addr));
        return false;
    }
    return plc->forceX(addr, turnOn);
}

void DeviceService::logReadFailure(const std::string& plcId, const std::string& point) {
    // 同一点位每 60 次（约 1 分钟）打一条，避免采集循环刷屏
    const std::string key = plcId + ":" + point;
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        count = ++failCounts_[key];
    }
    if (count == 1 || count % 60 == 0) {
        LOG_WARNING("[DeviceService] read failed, plcId=" + plcId + ", point=" + point
                    + ", count=" + std::to_string(count));
    }
}

void DeviceService::logKeyStateChanges(const PlcPointValues& points, const std::string& plcId) {
    if (table_.keyStatePoints.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(keyStateMutex_);
    PlcPointValues& baseline = keyStateBaseline_[plcId];

    for (const auto& signalName : table_.keyStatePoints) {
        const washcar::cfg::SignalConfig* signal = nullptr;
        for (const auto& candidate : table_.signals) {
            if (candidate.name == signalName) {
                signal = &candidate;
                break;
            }
        }
        if (signal == nullptr) {
            continue;
        }

        const auto parsed = parsePoint(signal->point);
        const std::string key = formatPoint(parsed.type, parsed.addr);
        if (key.empty() || !points.has(key)) {
            continue;
        }

        const int current = points.at(key);
        if (!baseline.has(key)) {
            baseline.set(key, current);   // 首轮只记基准
            continue;
        }
        if (baseline.at(key) != current) {
            LOG_INFO("[DeviceService] PLC_KEY_STATE plcId=" + plcId + " point=" + key
                     + " val=" + std::to_string(current) + ", reason=state_change");
            baseline.set(key, current);
        }
    }
}

// ============================ 业务层接口 ============================

PlcPointValues DeviceService::viewAllDeviceStatus() {
    // 只读内存缓存，**不发起任何 PLC 请求**。
    //
    // 这里是整个系统里最容易写错的一处：早先的实现在缓存为空时会同步 poll 一次，
    // 结果业务线程和采集线程抢同一把串口锁，命令响应被一整轮采集（几百 ms ~ 数秒）
    // 拖住；PLC 断线时缓存永远为空，更是每条命令都去开一次串口。
    //
    // 现在缓存空就返回空表，调用方会转成 WASHCAR_NO_PLC，语义清晰，
    // 也不会把"读不到"误判成"状态正常"。
    const std::string plcId = firstPlcId();
    PlcPointValues values;
    if (plcId.empty() || !snapshot(plcId, values)) {
        return PlcPointValues{};
    }
    return values;
}

bool DeviceService::forceFxM(const std::string& plcId, int addr, bool turnOn) {
    const bool ok = writeM(plcId, addr, turnOn);

    // 命令链最常用的几个点位，成功也记一条，方便现场按时间线对齐
    const bool isKey = (addr == 0 || addr == 100 || addr == 500 || addr == 1023);
    if (isKey || !ok) {
        LOG_INFO(std::string("[DeviceService] PLC_WRITE_RESULT point=m") + std::to_string(addr)
                 + ", plcId=" + plcId
                 + ", val=" + (turnOn ? "1" : "0")
                 + ", result=" + (ok ? "ok" : "FAIL"));
    }
    return ok;
}

bool DeviceService::forceFxX(const std::string& plcId, int addr, bool turnOn) {
    const bool ok = writeX(plcId, addr, turnOn);

    // X45 = 海天的启动信号
    const bool isKey = (addr == 45);
    if (isKey || !ok) {
        LOG_INFO(std::string("[DeviceService] PLC_WRITE_RESULT point=x") + std::to_string(addr)
                 + ", plcId=" + plcId
                 + ", val=" + (turnOn ? "1" : "0")
                 + ", result=" + (ok ? "ok" : "FAIL"));
    }
    return ok;
}

std::vector<std::pair<std::string, PlcPointValues>> DeviceService::getChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, PlcPointValues>> result;
    for (const auto& [plcId, entry] : cache_) {
        if (entry.changed) {
            result.emplace_back(plcId, entry.points);
        }
    }
    return result;
}

void DeviceService::clearChanged(const std::vector<std::string>& plcIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& plcId : plcIds) {
        const auto it = cache_.find(plcId);
        if (it != cache_.end()) {
            it->second.changed = false;
        }
    }
}

// ============================ 采集线程 ============================

void DeviceService::start() {
    if (running_.exchange(true)) {
        return;
    }

    // 起采集线程之前先同步采一轮，保证 start() 返回时缓存里已经有数据，
    // 后面任何线程第一次读都不会拿到空表（否则上电后第一个命令会被判成 no plc）。
    // 这是全系统**唯一**一处发生在采集线程之外的 PLC 读，位置固定、时机可控。
    if (poll()) {
        LOG_INFO("[DeviceService] first snapshot ready");
    } else {
        LOG_WARNING("[DeviceService] first snapshot failed, acquisition loop will retry");
    }

    if (!thread_.start("acquisition", [this]() { loop(); })) {
        LOG_ERROR("[DeviceService] 线程预算不足，采集未启动");
        running_.store(false);
        return;
    }
    LOG_INFO("[DeviceService] acquisition started, intervalMs=" + std::to_string(intervalMs_));
}

void DeviceService::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }

    // 采集统计：writtenPoints 远小于 freshPoints 说明"值没变就不写缓存"生效了。
    // 两者接近则意味着 PLC 点位一直在抖，或者缓存策略被改坏了。
    Stats s;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        s = stats_;
    }
    const double unchangedPct =
        s.freshPoints == 0 ? 0.0
                           : 100.0 * static_cast<double>(s.freshPoints - s.writtenPoints) /
                                 static_cast<double>(s.freshPoints);
    LOG_INFO("[DeviceService] acquisition stopped, polls=" + std::to_string(s.polls)
             + ", freshPoints=" + std::to_string(s.freshPoints)
             + ", writtenPoints=" + std::to_string(s.writtenPoints)
             + ", unchanged=" + std::to_string(static_cast<int>(unchangedPct)) + "%");
}

void DeviceService::loop() {
    using namespace std::chrono;

    // start() 已经同步采过一轮，这里从"下一个周期点"开始，
    // 避免上电瞬间连采两次（PLC 没接时日志里会出现两条重复的连接失败）。
    auto nextTick = steady_clock::now() + milliseconds(intervalMs_);

    while (running_.load()) {
        std::this_thread::sleep_until(nextTick);
        if (!running_.load()) {
            break;
        }

        const auto tickStart = steady_clock::now();
        if (!poll()) {
            // 采不到不打 INFO，PLC 拔线时会刷屏；上面已按点位限流告警
            LOG_DEBUG("[DeviceService] no data collected this cycle");
        }

        // 用"本拍开始时间 + 周期"推进，采集耗时从周期里扣掉，不会累积漂移。
        // 万一这一拍超时了（比如串口重试），下一拍立刻开始，不补拍防堆积。
        nextTick = tickStart + milliseconds(intervalMs_);
        const auto now = steady_clock::now();
        if (nextTick < now) {
            nextTick = now;
        }
    }
}
