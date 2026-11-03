#include "presentation/mqtt/downlink_pump.h"

#include "common/log/log_manager.h"
#include "presentation/protocol/command_parser.h"

#include <string>

DownlinkPump::DownlinkPump(MessageBuffer<RawMessage>& raw,
                           MessageBuffer<BusinessCommand>& biz,
                           const CommandParser& parser)
    : raw_(raw), biz_(biz), parser_(parser) {}

DownlinkPump::~DownlinkPump() {
    stop();
}

void DownlinkPump::start() {
    if (running_.exchange(true)) {
        return;
    }
    if (!thread_.start("downlink", [this]() { loop(); })) {
        LOG_ERROR("[DownlinkPump] 线程预算不足，下行处理未启动");
        return;
    }
    LOG_INFO("[DownlinkPump] started, tickMs=" + std::to_string(tickMs_));
}

void DownlinkPump::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    raw_.shutdown();
    if (thread_.joinable()) {
        thread_.join();
    }
    LOG_INFO("[DownlinkPump] stopped, dropped(raw)=" + std::to_string(raw_.droppedCount())
             + ", dropped(biz)=" + std::to_string(biz_.droppedCount()));
}

void DownlinkPump::loop() {
    while (running_.load()) {
        RawMessage raw;
        if (!raw_.popWait(raw, tickMs_)) {
            continue;
        }
        if (!running_.load()) {
            break;
        }

        std::string error;
        BusinessCommand cmd = parser_.parse(raw, error);
        if (cmd.type == CommandType::UNKNOWN) {
            // 解析不出来就地丢掉并留证：不往业务队列塞垃圾，业务层不用再判一次
            LOG_WARNING("[DownlinkPump] drop message, endpoint=" + raw.endpoint
                        + ", reason=" + error + ", payload=" + raw.payload);
            continue;
        }

        // 交给业务队列。队列满时 MessageBuffer 的策略是丢最旧并计数。
        biz_.push(std::move(cmd));
    }
}
