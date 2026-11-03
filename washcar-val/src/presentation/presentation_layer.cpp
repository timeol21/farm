#include "presentation/presentation_layer.h"

#include "common/buffer/message_buffer.h"
#include "common/buffer/uplink_buffer.h"
#include "common/config/config_manager.h"
#include "common/log/log_manager.h"
#include "common/message.h"
#include "common/time/time.h"
#include "presentation/protocol/command_parser.h"
#include "presentation/mqtt/downlink_pump.h"
#include "presentation/mqtt/uplink_pump.h"

#include <iostream>
#include <string>

PresentationLayer::PresentationLayer() = default;

PresentationLayer::~PresentationLayer() {
    stop();
}

bool PresentationLayer::init(MessageBuffer<RawMessage>& rawBuffer,
                             MessageBuffer<BusinessCommand>& bizBuffer,
                             UplinkBuffer& upBuffer) {
    auto& cfg = ConfigManager::instance();
    cfg_   = cfg.mqtt();
    boxNo_ = cfg.box().boxNo;
    rawBuffer_ = &rawBuffer;

    // 解析层：原始报文 -> 业务消息。协议方言全在 CommandParser 里。
    parser_ = std::make_unique<CommandParser>(cfg.box().boxType);

    // 下行：rawBuffer -> 解析 -> bizBuffer（注意这里**不直接调业务**）
    downlink_ = std::make_unique<DownlinkPump>(rawBuffer, bizBuffer, *parser_);

    // 上行：upBuffer -> publish
    uplink_ = std::make_unique<UplinkPump>(upBuffer, mqtt_);

    mqttEnabled_ = !cfg_.brokerIp.empty();
    if (!mqttEnabled_) {
        std::cout << "  - 未配置 brokerIp，MQTT 不启动（仅本地调试）" << std::endl;
    }
    return true;
}

bool PresentationLayer::start() {
    if (mqttEnabled_) {
        // MQTT 网络线程只做一件事：把收到的报文塞进原始队列，立刻返回。
        // 解析、业务全在下游线程，网络线程绝不阻塞。
        const bool ok = mqtt_.start(cfg_, boxNo_,
            [this](const std::string& endpoint, const std::string& payload) {
                if (rawBuffer_ == nullptr) {
                    return;
                }
                RawMessage raw;
                raw.endpoint = endpoint;
                raw.payload  = payload;
                raw.recvMs   = washcar::timeutil::nowMs();
                rawBuffer_->push(std::move(raw));
            });
        if (!ok) {
            LOG_ERROR("[Presentation] mqtt start failed");
            return false;
        }
        std::cout << "  - broker " << cfg_.brokerIp << ":" << cfg_.port
                  << ", clientId=" << boxNo_
                  << ", 分隔符 \"" << cfg_.topicSeparator << "\"" << std::endl;
    }

    downlink_->start();
    uplink_->start();
    return true;
}

void PresentationLayer::stop() {
    // 先停泵：不再产生新的 publish，也不再消费队列
    if (uplink_) {
        uplink_->stop();
    }
    if (downlink_) {
        downlink_->stop();
    }
    mqtt_.stop();
}
