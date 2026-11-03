#pragma once

#include "common/thread/thread_manager.h"

// 上行泵：按固定节拍从上行队列取报文，按类别选 endpoint 发布。
//
// 断链时不取消息，报文留在队列里：monitor / readdata 会被同类新报文顶掉，
// 队列自我收敛到"每类一条最新"；故障和回执逐条排队，重连后补发。

#include "presentation/mqtt/mqtt_client.h"
#include "common/buffer/uplink_buffer.h"

#include <atomic>
#include <thread>

class UplinkPump {
public:
    UplinkPump(UplinkBuffer& buffer, MqttClient& mqtt);
    ~UplinkPump();

    UplinkPump(const UplinkPump&) = delete;
    UplinkPump& operator=(const UplinkPump&) = delete;

    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    void loop();

    UplinkBuffer& buffer_;
    MqttClient& mqtt_;

    ManagedThread thread_;
    std::atomic<bool> running_{false};
    int tickMs_{50};
};
