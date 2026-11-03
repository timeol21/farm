#include "presentation/mqtt/uplink_pump.h"

#include "presentation/mqtt/endpoint_map.h"
#include "common/log/log_manager.h"

#include <chrono>
#include <string>

namespace {
/// 断链时的重试间隔。比 tickMs 长，避免每秒几十次空转判断
constexpr int kOfflineSleepMs = 200;
}  // namespace

UplinkPump::UplinkPump(UplinkBuffer& buffer, MqttClient& mqtt) : buffer_(buffer), mqtt_(mqtt) {}

UplinkPump::~UplinkPump() {
    stop();
}

void UplinkPump::start() {
    if (running_.exchange(true)) {
        return;
    }
    if (!thread_.start("uplink", [this]() { loop(); })) {
        LOG_ERROR("[UplinkPump] 线程预算不足，上行发送未启动");
        return;
    }
    LOG_INFO("[UplinkPump] started, tickMs=" + std::to_string(tickMs_));
}

void UplinkPump::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    buffer_.shutdown();
    if (thread_.joinable()) {
        thread_.join();
    }
    LOG_INFO("[UplinkPump] stopped, dropped=" + std::to_string(buffer_.droppedCount())
             + ", replaced=" + std::to_string(buffer_.replacedCount()));
}

void UplinkPump::loop() {
    while (running_.load()) {
        // 断了就先不取：报文留在队列里等重连，取出来又发不出去只是白丢
        if (!mqtt_.isConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kOfflineSleepMs));
            continue;
        }

        Report report;
        if (!buffer_.popWait(report, tickMs_)) {
            continue;
        }
        if (!running_.load()) {
            break;
        }

        const char* endpoint = comm::endpointFor(report.kind);
        if (!mqtt_.publish(endpoint, report.payload)) {
            LOG_WARNING(std::string("[UplinkPump] publish failed, endpoint=") + endpoint);
        }
    }
}
