#include "data_layer/device/repository.h"



DeviceRepository::DeviceRepository(){

}
    
DeviceRepository::~DeviceRepository(){

}


std::vector<HostNodeDefinition> DeviceRepository::loadHostNodes() {
    return hosts_;
}

std::vector<InterfaceDefinition> DeviceRepository::loadInterfaces() {
    return interfaces_;
}

std::vector<DeviceDefinition> DeviceRepository::loadDevices() {
    return devices_;
}


bool DeviceRepository::buildRawModel() {
    const auto& deviceServiceCfg = SystemConfig::instance().getDeviceServiceConfig();
    const auto& deviceConfig = deviceServiceCfg.config;

    // ===== HostNodes =====
    for (const auto& h : deviceConfig.getHostNode()) {
        RawHostNode node;
        node.id = h.id;
        node.name = h.name;
        node.hostType = h.hostType;
        node.ip = h.ip;
        raw_hosts_.push_back(node);
    }

    // ===== Interfaces =====
    for (const auto& i_pair : deviceConfig.getInterfaces()) {
        const auto& i = i_pair.second;
        RawInterface iface;
        iface.id = i.id;
        iface.type = i.type;
        iface.hostNodeId = i.hostNodeId;
        iface.serialPort.baud_rate = i.serialConfig.baud_rate;
        iface.serialPort.data_bits = i.serialConfig.data_bits;
        iface.serialPort.parity = i.serialConfig.parity;
        iface.serialPort.stop_bits = i.serialConfig.stop_bits;
        raw_interfaces_.push_back(std::move(iface));
    }

    // ===== Devices =====
    raw_devices_.clear();

    // ------------------------------
    // NVR
    // ------------------------------
    {
        const auto& nvr = deviceConfig.getNvr();
        if (!nvr.id.empty()) {  
            RawDevice dev;
            dev.id = nvr.id;
            dev.name = nvr.name;
            dev.deviceType = "nvr";
            dev.vendor = nvr.vendor;
            dev.driverKey = nvr.driverKey;
            dev.address = nvr.address;
            dev.hostNodeId = nvr.hostNodeId;
            dev.enabled = true;
            fillMetadata(dev.metadata, nvr.metadata);
            raw_devices_.emplace_back(std::move(dev));
        }
    }

    // ------------------------------
    // Camera
    // ------------------------------
    for (const auto& cam : deviceConfig.getCameras()) {
        RawDevice dev;
        dev.id = cam.id;
        dev.name = cam.name;
        dev.deviceType = "camera";
        dev.vendor = cam.vendor;
        dev.driverKey = cam.driverKey;
        dev.address = cam.address;
        dev.hostNodeId = cam.hostNodeId;
        dev.parentDeviceId = cam.parentDeviceId;
        dev.enabled = true;
        fillMetadata(dev.metadata, cam.metadata);
        raw_devices_.push_back(std::move(dev));
    }

    // ------------------------------
    // PLC
    // ------------------------------
    for (const auto& plc : deviceConfig.getPlcs()) {
        RawDevice dev;
        dev.id = plc.id;
        dev.name = plc.name;
        dev.deviceType = "plc";
        dev.vendor = plc.vendor;
        dev.driverKey = plc.driverKey;
        dev.address = plc.address;
        dev.hostNodeId = plc.hostNodeId;
        dev.interfaceId = plc.interfaceId;
        dev.bindingMode = plc.bindingMode;
        dev.enabled = true;
        fillMetadata(dev.metadata, plc.metadata);
        raw_devices_.push_back(std::move(dev));
    }

    // ------------------------------
    // PLC Device
    // ------------------------------
    for (const auto& plcDev : deviceConfig.getPlcDevices()) {
        RawDevice dev;
        dev.id = plcDev.id;
        dev.name = plcDev.name;
        dev.deviceType = "plc_device";
        dev.driverKey = plcDev.driverKey;
        dev.address = plcDev.address;
        dev.interfaceId = plcDev.interfaceId;
        dev.parentDeviceId = plcDev.parentDeviceId;
        dev.enabled = true;
        fillMetadata(dev.metadata, plcDev.metadata);
        raw_devices_.push_back(std::move(dev));
    }

    // ------------------------------
    // Sensor
    // ------------------------------
    for (const auto& sensor : deviceConfig.getSensors()) {
        RawDevice dev;
        dev.id = sensor.id;
        dev.name = sensor.name;
        dev.deviceType = "sensor";
        dev.driverKey = sensor.driverKey;
        dev.address = sensor.address;
        dev.interfaceId = sensor.interfaceId;
        dev.enabled = true;
        fillMetadata(dev.metadata, sensor.metadata);
        raw_devices_.push_back(std::move(dev));
    }

    // ------------------------------
    // GPIO
    // ------------------------------
    for (const auto& gpio : deviceConfig.getGpioDevices()) {
        RawDevice dev;
        dev.id = gpio.id;
        dev.name = gpio.name;
        dev.deviceType = "gpio";
        dev.driverKey = gpio.driverKey;
        dev.address = gpio.address;
        dev.interfaceId = gpio.interfaceId;
        dev.enabled = true;
        fillMetadata(dev.metadata, gpio.metadata);
        raw_devices_.push_back(std::move(dev));
    }

    return true;
}


void DeviceRepository::fillMetadata(std::unordered_map<std::string, std::string>& map,const DeviceMetadata& meta)
{
    map.clear();
    if (!meta.username.empty())      map["username"] = meta.username;
    if (!meta.password.empty())      map["password"] = meta.password;
    if (!meta.port.empty())          map["port"] = meta.port;
    if (!meta.rtsp_url.empty())      map["rtsp_url"] = meta.rtsp_url;
    if (!meta.channel.empty())       map["channel"] = meta.channel;
    if (!meta.slave_addr.empty())    map["slave_addr"] = meta.slave_addr;
    if (!meta.plc_port.empty())      map["plc_port"] = meta.plc_port;
    if (!meta.reg_start.empty())     map["reg_start"] = meta.reg_start;
    if (!meta.reg_count.empty())     map["reg_count"] = meta.reg_count;
    if (!meta.direction.empty())     map["direction"] = meta.direction;
    if (!meta.active_logic.empty())  map["active_logic"] = meta.active_logic;
    if (!meta.type.empty())          map["type"] = meta.type;
    if (!meta.logic.empty())         map["logic"] = meta.logic;
}


void DeviceRepository::printRawModels() const {
    std::cout << "\n=============================================" << std::endl;
    std::cout << "           打印 Raw 模型数据" << std::endl;
    std::cout << "=============================================\n" << std::endl;

    // --------------------------
    // 1. 打印 HostNodes
    // --------------------------
    std::cout << "=== RawHostNode 数量: " << raw_hosts_.size() << " ===" << std::endl;
    for (const auto& node : raw_hosts_) {
        std::cout << "HostNode: "
                  << "id=" << node.id << ", "
                  << "name=" << std::quoted(node.name) << ", "
                  << "hostType=" << std::quoted(node.hostType) << ", "
                  << "ip=" << std::quoted(node.ip)
                  << std::endl;
    }
    std::cout << std::endl;

    // --------------------------
    // 2. 打印 Interfaces
    // --------------------------
    std::cout << "=== RawInterface 数量: " << raw_interfaces_.size() << " ===" << std::endl;
    for (const auto& iface : raw_interfaces_) {
        std::cout << "Interface: "
                  << "id=" << iface.id << ", "
                  << "type=" << std::quoted(iface.type) << ", "
                  << "hostNodeId=" << iface.hostNodeId
                  << std::endl;
    }
    std::cout << std::endl;

    // --------------------------
    // 3. 打印 Devices（最复杂）
    // --------------------------
    std::cout << "=== RawDevice 数量: " << raw_devices_.size() << " ===" << std::endl;
    for (const auto& dev : raw_devices_) {
        std::cout << "Device: "
                  << "id=" << dev.id << ", "
                  << "name=" << std::quoted(dev.name) << ", "
                  << "deviceType=" << std::quoted(dev.deviceType) << ", "
                  << "vendor=" << std::quoted(dev.vendor) << ", "
                  << "driverKey=" << std::quoted(dev.driverKey) << ", "
                  << "address=" << std::quoted(dev.address) << ", "
                  << "hostNodeId=" << dev.hostNodeId << ", "
                  << "parentDeviceId=" << dev.parentDeviceId << ", "
                  << "interfaceId=" << dev.interfaceId << ", "
                  << "bindingMode=" << std::quoted(dev.bindingMode) << ", "
                  << "enabled=" << std::boolalpha << dev.enabled
                  << std::endl;

        // 打印 metadata
        if (!dev.metadata.empty()) {
            std::cout << "  Metadata: ";
            for (const auto& [k, v] : dev.metadata) {
                std::cout << "{" << k << ": " << v << "} ";
            }
            std::cout << std::endl;
        }
        std::cout << "-----------------------------------------" << std::endl;
    }

    std::cout << "\n========== 打印完成 ==========\n" << std::endl;
}



bool DeviceRepository::buildDomainModel() {
    devices_.clear();
    hosts_.clear();
    interfaces_.clear();

    // ===== HostNode =====
    for (const auto& r : raw_hosts_) {
        HostNodeDefinition h;
        h.id = r.id;
        h.name = r.name;
        h.hostType = r.hostType;
        h.ip = r.ip;

        hosts_.push_back(h);
    }

    // ===== Interface =====
    for (const auto& r : raw_interfaces_) {
        InterfaceDefinition i;
        i.id = r.id;
        i.endpoint = r.id;
        i.hostNodeId = r.hostNodeId;
        i.serialPort.baud_rate = r.serialPort.baud_rate;
        i.serialPort.data_bits = r.serialPort.data_bits;
        i.serialPort.parity = r.serialPort.parity;
        i.serialPort.stop_bits = r.serialPort.stop_bits;
        i.type = parseInterfaceType(r.type);

        interfaces_.push_back(i);
    }

    // ===== Device =====
    for (const auto& r : raw_devices_) {
        DeviceDefinition d;

        d.id = r.id;
        d.name = r.name;

        // ⭐ 关键：先转 enum
        DeviceType type = parseDeviceType(r.deviceType);
        d.deviceTypeEnum = type;   
        d.deviceType = r.deviceType; 

        d.vendor = r.vendor;
        d.driverKey = r.driverKey;
        d.address = r.address;

        d.hostNodeId = r.hostNodeId;
        d.interfaceId = r.interfaceId;
        d.parentDeviceId = r.parentDeviceId;

        d.bindingMode = parseBindingMode(r);

        // ⭐ 用 enum 来做 role 映射（关键升级）
        d.role = mapRoleFromType(type, r);

        d.metadata = r.metadata;

        d.enabled = r.enabled;

        if (d.hostNodeId.empty()) {
            throw std::runtime_error(std::string("device missing hostNodeId: ") + d.name);
        }

        devices_.push_back(d);
    }

    return true;
}  


InterfaceType DeviceRepository::parseInterfaceType(const std::string& type) {
    if (type == "serial") return InterfaceType::Serial;
    if (type == "rs485")  return InterfaceType::RS485;
    if (type == "usb")    return InterfaceType::USB;
    if (type == "gpio")   return InterfaceType::GPIO;
    if (type == "sdk")    return InterfaceType::SDK;
    if (type == "virtual")return InterfaceType::Virtual;

    return InterfaceType::Ethernet;
}



RuntimeBindingMode DeviceRepository::parseBindingMode(const RawDevice& r) {
    // 1. 明确写了 bindingMode
    if (!r.bindingMode.empty()) {
        if (r.bindingMode == "direct")
            return RuntimeBindingMode::Independent;
        if (r.bindingMode == "gateway")
            return RuntimeBindingMode::Dependent;
    }

    // 2. 有父设备 → 一定是依附
    if (!r.parentDeviceId.empty()) {
        return RuntimeBindingMode::Dependent;
    }

    // 3. 默认：独立
    return RuntimeBindingMode::Independent;
}


bool DeviceRepository::hasChildren(const std::string& id) {
    for (const auto& r : raw_devices_) {
        if (r.parentDeviceId == id)
            return true;
    }
    return false;
}

DeviceRole DeviceRepository::mapRoleFromType(DeviceType type, const RawDevice& r) {

    switch (type) {
        case DeviceType::CAMERA:
        case DeviceType::SENSOR:
        case DeviceType::RADAR:
            return DeviceRole::Sensor;

        case DeviceType::PLC:
            return hasChildren(r.id) ? DeviceRole::Composite : DeviceRole::Actuator;

        case DeviceType::NVR:
            return DeviceRole::Gateway;

        case DeviceType::GPIO:
            return DeviceRole::Actuator;

        default:
            return DeviceRole::Sensor;
    }
}

DeviceType DeviceRepository::parseDeviceType(const std::string& type) {
    if (type == "plc") return DeviceType::PLC;
    if (type == "sensor") return DeviceType::SENSOR;
    if (type == "camera") return DeviceType::CAMERA;
    if (type == "nvr") return DeviceType::NVR;
    if (type == "gpio_device") return DeviceType::GPIO;
    if (type == "radar") return DeviceType::RADAR;
    if (type == "plc_device") return DeviceType::PLC_DEVICE;

    return DeviceType::UNKNOWN;
}

DeviceRole DeviceRepository::parseDeviceRole(const RawDevice& r) {

    // ===== Gateway（网关类）=====
    if (r.deviceType == "nvr" ||
        r.deviceType == "plc_gateway") {
        return DeviceRole::Gateway;
    }

    // ===== Composite（容器型）=====
    if (r.deviceType == "plc" &&
        hasChildren(r.id)) {
        return DeviceRole::Composite;
    }

    // ===== Sensor =====
    if (r.deviceType == "sensor" ||
        r.deviceType == "camera" ||
        r.deviceType == "radar") {
        return DeviceRole::Sensor;
    }

    // ===== Actuator =====
    if (r.deviceType == "plc_device" ||
        r.deviceType == "gpio_device") {
        return DeviceRole::Actuator;
    }

    // ===== LogicalPoint（逻辑点）=====
    if (!r.parentDeviceId.empty() &&
        r.deviceType == "point") {
        return DeviceRole::LogicalPoint;
    }

    // 默认
    return DeviceRole::Sensor;
}