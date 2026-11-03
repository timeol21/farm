#include "business_layer/report/report_scheduler.h"

#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "common/time/time.h"

#include <algorithm>
#include <chrono>

namespace {
// 主循环节拍。比最小的上报周期（100ms 下限）还细一档，
// 保证到点误差不超过 100ms，CPU 占用可忽略。
constexpr int kTickMs = 100;

// 日报检查的上限间隔。目标分钟只有 60 秒，检查间隔必须远小于它，
// 否则会整段跨过去导致漏报。配置里填多大都夹到这个值。
constexpr int kMaxDailyCheckMs = 5000;
}  // namespace

ReportScheduler::ReportScheduler(IWashCarService& service) : service_(service) {
    const auto& timing = ConfigManager::instance().timing();

    valueChangeIntervalMs_ = timing.valueChangeIntervalMs > 0 ? timing.valueChangeIntervalMs : 3000;
    faultReportIntervalMs_ = timing.faultReportIntervalMs > 0 ? timing.faultReportIntervalMs : 3000;
    dailyCheckIntervalMs_  = timing.dailyReportCheckIntervalMs > 0 ? timing.dailyReportCheckIntervalMs : 60000;

    // 与 config_manager 的下限保护保持一致：过小会刷屏/抢 CPU
    valueChangeIntervalMs_ = std::max(valueChangeIntervalMs_, 100);
    faultReportIntervalMs_ = std::max(faultReportIntervalMs_, 100);
    dailyCheckIntervalMs_  = std::max(dailyCheckIntervalMs_, 1000);
    dailyCheckIntervalMs_  = std::min(dailyCheckIntervalMs_, kMaxDailyCheckMs);

    dailyReportHour_   = timing.dailyReportHour;
    dailyReportMinute_ = timing.dailyReportMinute;
}

ReportScheduler::~ReportScheduler() {
    stop();
}

void ReportScheduler::start() {
    if (running_.exchange(true)) {
        return;
    }
    if (!thread_.start("report", [this]() { loop(); })) {
        LOG_ERROR("[Report] 线程预算不足，上报调度未启动");
        running_.store(false);
        return;
    }
    LOG_INFO("[Report] scheduler started, valueChange=" + std::to_string(valueChangeIntervalMs_)
             + "ms, fault=" + std::to_string(faultReportIntervalMs_) + "ms");
}

void ReportScheduler::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    LOG_INFO("[Report] scheduler stopped");
}

void ReportScheduler::loop() {
    using clock = std::chrono::steady_clock;

    auto nextValueChange = clock::now();
    auto nextFaultReport = clock::now();
    auto nextDailyCheck  = clock::now();

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
        if (!running_.load()) {
            break;
        }

        const auto now = clock::now();

        if (now >= nextValueChange) {
            nextValueChange = now + std::chrono::milliseconds(valueChangeIntervalMs_);
            service_.executeValueChangeTask();
        }

        if (now >= nextFaultReport) {
            nextFaultReport = now + std::chrono::milliseconds(faultReportIntervalMs_);
            service_.executeFaultReportTask();
        }

        if (now >= nextDailyCheck) {
            nextDailyCheck = now + std::chrono::milliseconds(dailyCheckIntervalMs_);

            const std::string today = washcar::timeutil::todayDate();
            if (today != lastDailyReportDate_ &&
                washcar::timeutil::isAtMinute(dailyReportHour_, dailyReportMinute_)) {
                lastDailyReportDate_ = today;
                LOG_INFO("[Report] 触发每日物料上报, date=" + today);
                service_.executeDailyReportTask();
            }
        }
    }
}
