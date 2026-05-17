#include "config/config_parser.h"
#include <fstream>
#include <iostream>

ConfigParser& ConfigParser::getInstance() {
    static ConfigParser instance;
    return instance;
}

bool ConfigParser::loadConfig(const std::string& filePath) {
    try{
        config_ = AppConfig{};
        std::ifstream file(filePath);
        if(!file.is_open()) {
            std::cerr << "Failed to open config file: " << filePath <<std::endl;
            return false;
        }

        nlohmann::json j;
        file >> j;
        if(!parseChannels(j["channels"])) return false;
        if(!parseDevices(j["devices"])) return false;
        if(!parseMqtt(j["mqtt"])) return false;
        if(!parseSystem(j["system"])) return false;
        return true;
    } catch(const std::exception& e){
        std::cerr << "Exception while loading config: " << e.what() << std::endl;
        return false;
    }
}

const AppConfig& ConfigParser::getConfig() const {
    return config_;
}

bool ConfigParser::parseChannels(const nlohmann::json& j) {
    for (const auto& jChan : j) {
        ChannelConfig ch;
        ch.id = jChan["id"];
        ch.type = jChan["type"];
        ch.port = jChan.value("port", "");
        ch.baudrate = jChan.value("baudrate", 9600);
        ch.parity = jChan.value("parity", "N");
        ch.stopBits = jChan.value("stopBits", 1);
        ch.dataBits = jChan.value("dataBits", 8);
        config_.channels.push_back(ch);
    }
    return true;
}

bool ConfigParser::parseDevices(const nlohmann::json& j) {
    for(const auto& jDev : j){
        DeviceConfig dev;
        dev.id = jDev["id"];
        dev.type = jDev["type"];
        dev.model = jDev["model"];

        dev.protocol = jDev["protocol"];
        dev.channelId = jDev["channelId"];
        dev.interval = jDev.value("interval", 60000);
        dev.modbus_slave_id = jDev.value("modbus_slave_id", 1);
        for (const auto& ch : config_.channels) {
            if (ch.id == dev.channelId) {
                dev.channelConfig = ch;
                break;
            }
        }
        if(jDev.contains("parser")){
            const auto& jParser = jDev["parser"];
            dev.parser.type = jParser["type"];
            dev.parser.source = jParser["source"];
            dev.parser.min_length = jParser["frame"]["min_length"];

            for (const auto& jField : jParser["fields"]) {
                ParseField field;
                field.key = jField["key"];
                field.byte_offset = jField["byte_offset"];
                field.bytes = jField["bytes"];
                field.signed_flag = jField["signed"];
                field.endian = jField["endian"];
                field.ratio = jField["ratio"];
                field.offset = jField["offset"];
                field.unit = jField["unit"];

                dev.parser.fields.push_back(field);
            }
        }

        if(jDev.contains("control")) {
            const auto& jControl = jDev["control"];
            dev.control.kind = jControl.value("kind", "");
            if(jControl.contains("commands")) {
                for(const auto& jCmd : jControl["commands"]) {
                    ControlCommandConfig cmd;
                    cmd.name = jCmd.value("name", "");
                    cmd.register_address = jCmd.value("register_address", 0);
                    cmd.value = jCmd.value("value", 0);
                    cmd.function_code = jCmd.value("function_code", 5);
                    if (!cmd.name.empty()) {
                        dev.control.commands[cmd.name] = cmd;
                    }
                }
            }
            if(jControl.contains("feedback")) {
                const auto& jFeedback = jControl["feedback"];
                dev.control.feedback.register_address = jFeedback.value("register_address", 0);
                dev.control.feedback.function_code = jFeedback.value("function_code", 1);
            }
        }

        config_.devices.push_back(dev);
    }
    return true;
}

bool ConfigParser::parseMqtt(const nlohmann::json& jMqtt) {
    config_.mqtt.broker = jMqtt["broker"];
    config_.mqtt.client_id = jMqtt["client_id"];
    config_.mqtt.status_report_topic = jMqtt.value("status_report_topic", "");
    for (const auto& sub : jMqtt["subscriptions"]) {
        MqttSubscription s;
        s.topic = sub["topic"];
        s.qos = sub["qos"];
        config_.mqtt.subscriptions.push_back(s);
    }
    return true;
}

bool ConfigParser::parseSystem(const nlohmann::json& jSys) {
    config_.system.auto_report_enabled = jSys.value("auto_report_enabled", false);
    config_.system.auto_report_interval_ms = jSys.value("auto_report_interval_ms", 60000);
    for (const auto& cmd : jSys["allowed_commands"]) {
        config_.system.allowed_commands.push_back(cmd);
    }
    return true;
}