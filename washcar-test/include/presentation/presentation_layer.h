#pragma once

// 表现层（含解析层）聚合根。
//
// 这一层负责"和外界换消息"的全部机械动作，业务层不碰 MQTT、不认识 json 字段名：
//   - MQTT 连接（连接、重连、订阅、发布）
//   - 收包后立刻入**原始报文队列**，网络线程里不做任何业务
//   - 下行泵：原始队列 -> `CommandParser` 解析成 `BusinessCommand` -> **业务消息队列**
//   - 上行泵：上行队列 -> `endpointFor(kind)` -> publish
//
// 注意下行是**两段队列、两级消费者**：
//   原始队列负责削峰（网络突发不该压到解析上），
//   业务队列负责解耦（解析失败/业务卡住互不影响，且业务层可以有自己的节奏）。
//
// main 只调 init / start / stop。

#include "presentation/mqtt/mqtt_client.h"

#include <memory>
#include <string>

class BusinessLayer;
class CommandParser;
class DownlinkPump;
class UplinkPump;
class UplinkBuffer;
template <typename T>
class MessageBuffer;
struct RawMessage;
struct BusinessCommand;

class PresentationLayer {
public:
    PresentationLayer();
    ~PresentationLayer();

    PresentationLayer(const PresentationLayer&) = delete;
    PresentationLayer& operator=(const PresentationLayer&) = delete;

    /// 三个队列都由 main 创建，生命周期需长于本对象。
    ///   rawBuffer —— MQTT 回调写入，下行泵读
    ///   bizBuffer —— 下行泵写入，业务层分派线程读
    ///   upBuffer  —— 业务层写入，上行泵读
    bool init(MessageBuffer<RawMessage>& rawBuffer,
              MessageBuffer<BusinessCommand>& bizBuffer,
              UplinkBuffer& upBuffer);

    /// 建立 MQTT 连接并启动两个泵。broker 不可达时返回 false。
    bool start();
    void stop();

    bool isConnected() const { return mqtt_.isConnected(); }

private:
    MqttClient mqtt_;
    std::unique_ptr<CommandParser> parser_;
    std::unique_ptr<DownlinkPump> downlink_;
    std::unique_ptr<UplinkPump> uplink_;

    MessageBuffer<RawMessage>* rawBuffer_{nullptr};

    washcar::cfg::MqttConfig cfg_;
    std::string boxNo_;
    bool mqttEnabled_{false};
};
