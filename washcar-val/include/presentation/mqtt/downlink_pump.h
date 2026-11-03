#pragma once

#include "common/thread/thread_manager.h"

// 下行泵（属于表现层）：原始队列 -> 解析 -> 业务消息队列。
//
// 为什么单独一个线程：
//   原来命令是在 mosquitto 的接收线程里同步执行的，驿公里启动命令含
//   sleep(200ms) + 最长 3s 的确认轮询，会把 MQTT 线程占住 3 秒多。
//   现在 MQTT 线程只负责入队，解析在泵线程，收发不受影响。
//
// 为什么解析完再入一次队、而不是直接调业务层：
//   1. 业务层可以有**自己的节奏**（它可能正卡在一条启动命令的 3 秒确认里），
//      解析线程不该跟着一起停；
//   2. 解析层的崩溃/慢解析不会波及业务，反之亦然；
//   3. 两条队列在日志里能看到各自的积压量，现场定位"是收不动还是处理不动"。
//
// 节拍：popWait(tickMs) —— 空队列最多等 tickMs，有消息立即取。

#include "common/buffer/message_buffer.h"
#include "common/message.h"

#include <atomic>

class CommandParser;

class DownlinkPump {
public:
    /// raw 由 MQTT 回调写入；biz 由本泵写入、业务层读
    DownlinkPump(MessageBuffer<RawMessage>& raw,
                 MessageBuffer<BusinessCommand>& biz,
                 const CommandParser& parser);
    ~DownlinkPump();

    DownlinkPump(const DownlinkPump&) = delete;
    DownlinkPump& operator=(const DownlinkPump&) = delete;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    void loop();

    MessageBuffer<RawMessage>& raw_;
    MessageBuffer<BusinessCommand>& biz_;
    const CommandParser& parser_;

    ManagedThread thread_;
    std::atomic<bool> running_{false};
    int tickMs_{50};
};
