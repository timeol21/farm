#include "common/config/config_load.h"
#include "common/config/config_object.h"
SystemConfig& SystemConfig::instance(){
    static SystemConfig instance;
    return instance;
 }


SystemConfig::SystemConfig(): m_initialized(false) {

}

bool SystemConfig::init(const std::string& path){
    if (m_initialized) {
            std::cout << "[SystemConfig] 已经初始化过，无需重复加载" << std::endl;
            return true;
        }

        // 加载并解析系统配置
        if (!load(path)) {
            std::cerr << "[SystemConfig] 配置加载失败" << std::endl;
            return false;
        }

        // ✅ 关键：系统配置加载完成后，初始化 DeviceConfig
        if(!initDeviceConfig()){
            std::cerr << "[SystemConfig] 配置设备信息加载失败" << std::endl;
            return false;
        
        }

        m_initialized = true;
        std::cout << "[SystemConfig] 全局初始化完成" << std::endl;
        return true;
}

bool SystemConfig::initDeviceConfig(){

    if(!services.device_service.config.load()){
        //日志
        return false;
    }
    return true;

}

bool SystemConfig::load(const std::string& path) {
    json j;
    if (!JsonUtil::readJsonFile(path, j)) {
        std::cerr << "加载系统配置文件失败: " << path << std::endl;
        return false;
    }

    try {
        parse(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析系统配置失败: " << e.what() << std::endl;
        return false;
    }
}

void SystemConfig::parse(const json& j) {
    // 基础信息
    version = j["version"];
    description = j["description"];

    // system
    system.name = j["system"]["name"];
    system.mode = j["system"]["mode"];

    // services
    const auto& s = j["services"];

    // unclog_service
    services.unclog_service.enabled = s["unclog_service"]["enabled"];
    services.unclog_service.check_interval_ms = s["unclog_service"]["check_interval_ms"];
    services.unclog_service.auto_trigger = s["unclog_service"]["auto_trigger"];
    services.unclog_service.timeout_ms = s["unclog_service"]["timeout_ms"];

    // device_service
    services.device_service.enabled = s["device_service"]["enabled"];
    services.device_service.polling_interval_ms = s["device_service"]["polling_interval_ms"];
    services.device_service.hardware.gpio_chip = s["device_service"]["hardware"]["gpio_chip"];
    services.device_service.config.updateDeviceConfigPath(s["device_service"]["config"]["device_config_path"]);

    // detection_service 
    services.detection_service.ai.enabled = s["detection_service"]["ai"]["enabled"];
    services.detection_service.ai.model_path = s["detection_service"]["ai"]["model_path"];
    services.detection_service.ai.confidence_threshold = s["detection_service"]["ai"]["confidence_threshold"];
    services.detection_service.ai.nms_threshold = s["detection_service"]["ai"]["nms_threshold"];

    // web_service
    services.web_service.enabled = s["web_service"]["enabled"];
    services.web_service.ip = s["web_service"]["ip"];
    services.web_service.port = s["web_service"]["port"];
    services.web_service.thread_pool_size = s["web_service"]["thread_pool_size"];

    // mqtt_service
    services.mqtt_service.enabled = s["mqtt_service"]["enabled"];
    services.mqtt_service.broker_ip = s["mqtt_service"]["broker_ip"];
    services.mqtt_service.port = s["mqtt_service"]["port"];
    services.mqtt_service.client_id = s["mqtt_service"]["client_id"];
    services.mqtt_service.qos = s["mqtt_service"]["qos"];

    // database
    services.database.enabled = s["database"]["enabled"];
    services.database.type = s["database"]["type"];
    services.database.path = s["database"]["path"];

    // logging
    logging.level = j["logging"]["level"];
    logging.path = j["logging"]["path"];
    logging.max_size_mb = j["logging"]["max_size_mb"];
    logging.max_files = j["logging"]["max_files"];
}



std::string SystemConfig::getVersion() const {
    return version;
}

std::string SystemConfig::getDescription() const {
    return description;
}

const SystemInfo& SystemConfig::getSystem() const {
    return system;
}

const Services& SystemConfig::getServices() const {
    return services;
}

const Logging& SystemConfig::getLogging() const {
    return logging;
}

const DeviceServiceConfig& SystemConfig::getDeviceServiceConfig() const {
    return services.device_service;
}


void DeviceConfig::updateDeviceConfigPath(const std::string& path){
    if(path.empty()) return ;
    config_path_ = path;
}


bool DeviceConfig::load(){
    json j;
    if (!JsonUtil::readJsonFile(config_path_, j)) {
        std::cerr << "加载设备配置失败: " << config_path_ << std::endl;
        return false;
    }

    try {
        parse(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析设备配置失败: " << e.what() << std::endl;
        return false;
    }
}


void DeviceConfig::parse(const json& j){
    version_ = j.value("version", "");
    description_ = j.value("description", "");
    // 解析主机节点
    parseHostNodes(j);

    // 解析所有接口（serial/gpio/ethernet）
    parseInterfaces(j);

    // 解析所有设备
    parseDevices(j);
}

// ===================== 解析 host_nodes =====================
void DeviceConfig::parseHostNodes(const json& j) {
    if (!j.contains("host_nodes") || !j["host_nodes"].is_array()) {
        return;
    }

    host_node_.clear();

    for (const auto& hostNodeJson : j["host_nodes"]) {
        HostNode node;
        node.id = hostNodeJson.value("id", "");
        node.name = hostNodeJson.value("name", "");
        node.hostType = hostNodeJson.value("hostType", "");
        node.ip = hostNodeJson.value("ip", "");
        
        host_node_.push_back(std::move(node));
    }
}

// ===================== 解析 interfaces =====================
void DeviceConfig::parseInterfaces(const json& j) {
    if (!j.contains("interfaces") || !j["interfaces"].is_array()) {
        return;
    }

    for (const auto& item : j["interfaces"]) {
        InterfaceConfig intf;
        intf.id = item.value("id", "");
        intf.type = item.value("type", "");
        intf.hostNodeId = item.value("hostNodeId", "");

        if (intf.type == "serial" && item.contains("config")) {
            intf.serialConfig = parseSerialConfig(item["config"]);
        }

        interfaces_[intf.id] = intf;
    }
}

// ===================== 解析串口配置 =====================
SerialPortConfig DeviceConfig::parseSerialConfig(const json& j) {
    SerialPortConfig cfg;
    cfg.baud_rate = j.value("baud_rate", 9600);
    cfg.data_bits = j.value("data_bits", 8);
    cfg.stop_bits = j.value("stop_bits", 1);
    cfg.parity = j.value("parity", "none");
    cfg.read_timeout_ms = j.value("read_timeout_ms", 100);
    cfg.max_retries = j.value("max_retries", 3);
    return cfg;
}

// ===================== 解析元数据（完全按你原来的写法） =====================
DeviceMetadata DeviceConfig::parseMetadata(const json& j) {
    DeviceMetadata meta;
    if (!j.is_object()) return meta;

    meta.username = j.value("username", "");
    meta.password = j.value("password", "");
    meta.port = j.value("port", "");
    meta.channel = j.value("channel", "");
    meta.rtsp_url = j.value("rtsp_url", "");
    meta.slave_addr = j.value("slave_addr", "");
    meta.plc_port = j.value("plc_port", "");
    meta.reg_start = j.value("reg_start", "");
    meta.reg_count = j.value("reg_count", "");
    meta.direction = j.value("direction", "");
    meta.active_logic = j.value("active_logic", "");
    meta.type = j.value("type", "");
    meta.logic = j.value("logic", "");

    if (j.contains("commands")) {
        for (const auto& cmd : j["commands"].items()) {
            meta.commands[cmd.key()] = cmd.value().get<std::vector<std::string>>();
        }
    }
    return meta;
}

// ===================== 统一分发设备解析 =====================
void DeviceConfig::parseDevices(const json& j) {
    if (!j.contains("devices") || !j["devices"].is_array()) {
        return;
    }

    for (const auto& dev : j["devices"]) {
        std::string type = dev.value("deviceType", "");

        if (type == "nvr") {
            nvr_ = parseNvr(dev);
        }
        else if (type == "camera") {
            cameras_.push_back(parseCamera(dev));
        }
        else if (type == "plc") {
            plcs_.push_back(parsePlc(dev));
        }
        else if (type == "plc_device") {
            plc_devices_.push_back(parsePlcDevice(dev));
        }
        else if (type == "sensor") {
            sensors_.push_back(parseSensor(dev));
        }
        else if (type == "gpio_device") {
            gpio_devices_.push_back(parseGpioDevice(dev));
        }
    }
}

// ===================== 单个设备解析函数（每个设备一个方法） =====================

NvrConfig DeviceConfig::parseNvr(const json& j) {
    NvrConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.vendor = j.value("vendor", "");
    cfg.hostNodeId = j.value("hostNodeId", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}

CameraConfig DeviceConfig::parseCamera(const json& j) {
    CameraConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.vendor = j.value("vendor", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.parentDeviceId = j.value("parentDeviceId", "");
    cfg.hostNodeId = j.value("hostNodeId", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}

PlcConfig DeviceConfig::parsePlc(const json& j) {
    PlcConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.vendor = j.value("vendor", "");
    cfg.hostNodeId = j.value("hostNodeId", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.bindingMode = j.value("bindingMode", "");
    cfg.interfaceId = j.value("interfaceId", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}

PlcDeviceConfig DeviceConfig::parsePlcDevice(const json& j) {
    PlcDeviceConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.parentDeviceId = j.value("parentDeviceId", "");
    cfg.interfaceId = j.value("interfaceId", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}

SensorConfig DeviceConfig::parseSensor(const json& j) {
    SensorConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.interfaceId = j.value("interfaceId", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}

GpioDeviceConfig DeviceConfig::parseGpioDevice(const json& j) {
    GpioDeviceConfig cfg;
    cfg.id = j.value("id", "");
    cfg.name = j.value("name", "");
    cfg.deviceType = j.value("deviceType", "");
    cfg.driverKey = j.value("driverKey", "");
    cfg.interfaceId = j.value("interfaceId", "");
    cfg.address = j.value("address", "");
    cfg.metadata = parseMetadata(j);
    return cfg;
}





const std::vector<HostNode>& DeviceConfig::getHostNode() const { return host_node_; }

const std::map<std::string, InterfaceConfig>& DeviceConfig::getInterfaces() const { return interfaces_; }
const NvrConfig& DeviceConfig::getNvr() const { return nvr_; }
const std::vector<CameraConfig>& DeviceConfig::getCameras() const { return cameras_; }
const std::vector<PlcConfig>& DeviceConfig::getPlcs() const { return plcs_; }
const std::vector<PlcDeviceConfig>& DeviceConfig::getPlcDevices() const { return plc_devices_; }
const std::vector<SensorConfig>& DeviceConfig::getSensors() const { return sensors_; }
const std::vector<GpioDeviceConfig>& DeviceConfig::getGpioDevices() const { return gpio_devices_; }