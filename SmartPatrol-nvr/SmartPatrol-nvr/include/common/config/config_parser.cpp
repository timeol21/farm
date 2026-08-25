#include "config_parser.h"
#include "config_info.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// 单例（保持不变）
ConfigParser& ConfigParser::getInstance()
{
    static ConfigParser instance;
    return instance;
}

// 加载配置文件（主逻辑）
bool ConfigParser::loadFromFile(const std::string& path)
{
    if (isLoaded_) {
        return true;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "[ConfigParser] 无法打开配置文件: " << path << "\n";
        return false;
    }

    json j;
    try {
        ifs >> j;
    } catch (std::exception& e) {
        std::cerr << "[ConfigParser] JSON parse error: " << e.what() << "\n";
        return false;
    }
        auto& root = j["device_config"];
        config_.version = root.value("version", "");
        config_.description = root.value("description", "");
        config_.boxId = root.value("boxId","");
        auto& devs = root["devices"];
        parseNVR(devs);
        parseCameras(devs);
        isLoaded_ = true;
        return true;
}       

// 解析NVR（重点：string nvrId判空，int port判0）
void ConfigParser::parseNVR(const json& j)
{
    NVRConfig nvr;
    if (!j.contains("nvr")) {
        // throw std::runtime_error("devices节点下缺少nvr子节点");
    }
    const json& nvrJson = j["nvr"];
    // 解析string类型nvrId（直接读字符串）
    nvr.nvrId = nvrJson.value("nvrId", "");          
    nvr.brand = nvrJson.value("brand", "");         
    nvr.ip = nvrJson.value("ip", "");              
    nvr.username = nvrJson.value("username", "");   
    nvr.password = nvrJson.value("password", "");   
    // 解析int类型port（JSON自动转整型）
    nvr.port = nvrJson.value("port", 0);           

    // 校验：string nvrId判空，int port判0
    if (nvr.nvrId.empty()) { // 字符串ID判空
        throw std::runtime_error("NVR配置缺少有效nvrId（字符串）");
    }
    if (nvr.ip.empty()) {
        throw std::runtime_error("NVR配置缺少ip字段");
    }
    if (nvr.port == 0) { // 整型端口判0
        throw std::runtime_error("NVR配置缺少有效port（整型）");
    }
    config_.nvr = nvr;
}

// 解析摄像头（重点：string cameraId/nvrId判空）
void ConfigParser::parseCameras(const json& j)
{
    config_.cameras.clear();
    if (!j.contains("camera")) {
        std::cerr << "[ConfigParser] devices节点下缺少camera子节点\n";
        return;
    }

    for (const auto& item : j["camera"]) {
        CameraConfig camera;
        // 解析string类型ID（直接读字符串）
        camera.cameraId = item.value("cameraId", "");  
        camera.nvrId = item.value("nvrId", "");        
        camera.name = item.value("name", ""); 
             
        camera.channelNo = 0; // 整型通道号初始化为0
        // 校验：string ID判空（不再判0）
        if (camera.cameraId.empty()) { // 字符串ID判空
            std::cerr << "[ConfigParser] 摄像头配置缺少有效cameraId（字符串），跳过\n";
            continue;
        }
        if (camera.nvrId.empty()) { // 字符串ID判空
            std::cerr << "[ConfigParser] 摄像头[" << camera.cameraId << "]缺少有效nvrId（字符串），跳过\n";
            continue;
        }
        config_.cameras.push_back(camera);
        camera.IPCIP  = item.value("IPCIP","");
        camera.port = item.value("port",0);
        camera.IPCUSER  = item.value("IPCUSER","");
        camera.IPCPWD  = item.value("IPCPWD","");

    }
    // 关键：解析完摄像头后立即分配通道号
    if (!config_.cameras.empty()) {
        CameraChangeChannelNo(config_.cameras);
    } else {
        std::cerr << "[ConfigParser] 无有效摄像头配置，跳过通道号分配\n";
    }
}

// 辅助函数：将cameraId转为整数（非数字ID返回极大值，排到最后）
int ConfigParser::cameraIdToInt(const std::string& cameraId) {
    // 检查是否为纯数字
    for (char c : cameraId) {
        if (!isdigit(c)) {
            std::cerr << "[ConfigParser] 摄像头ID[" << cameraId << "]非纯数字，排到最后\n";
            return INT_MAX; // 非数字ID返回极大值
        }
    }
    return atoi(cameraId.c_str()); // 数字ID转为整数
}

void ConfigParser::CameraChangeChannelNo(std::vector<CameraConfig>& cameras) {
    if (cameras.empty()) {
        std::cerr << "[ConfigParser] 摄像头列表为空，无需分配通道号\n";
        return;
    }

    // 步骤1：按cameraId数值升序排序
    std::sort(cameras.begin(), cameras.end(), [this](const CameraConfig& a, const CameraConfig& b) {
        int idA = this->cameraIdToInt(a.cameraId);
        int idB = this->cameraIdToInt(b.cameraId);
        return idA < idB; // 升序排列
    });

    // 步骤2：从33开始分配通道号（修正原代码初始值32的笔误）
    int channelNo = 33; 
    for (auto& camera : cameras) {
        camera.channelNo = channelNo++;
        std::cout << "[ConfigParser] 摄像头[ID:" << camera.cameraId << "] 分配通道号: " << camera.channelNo << "\n";
    }
}
// ---------------- GPIO Sensors (红外、水浸、烟感) ----------------
void ConfigParser::parseGPIOSensors(const json& j)
{
    if (!j.contains("gpio_sensor")) return;

    for (auto& item : j["gpio_sensor"]) {
        GPIOSensorConfig s;
        s.id = item.value("id", "");
        s.name = item.value("name", "");
        s.type = item.value("type", "");
        s.gpioPin = item.value("gpio_pin", 0);
        s.description = item.value("description", "");

        config_.gpioSensors.push_back(s);
    }
}

// ---------------- 温湿度传感器 ----------------
void ConfigParser::parseTempHumiditySensor(const json& j) {
    if (!j.contains("temp_humidity_sensor")) return;
    auto& ths = j["temp_humidity_sensor"];

    // 1. 解析串口（用于初始化串口通信）
    if (ths.contains("serial_config")) {
        auto& sc = ths["serial_config"];
        config_.tempHumiditySensor.serial.port = sc.value("port", "/dev/ttyS3");
        config_.tempHumiditySensor.serial.baudRate = sc.value("baud_rate", 9600);
    }

    // 2. 解析传感器列表（映射 JSON 键名到结构体成员）
    if (ths.contains("sensors")) {
        for (auto& item : ths["sensors"]) {
            TempHumiditySensorConfig s;
            // 重点：在这里完成 JSON 键名到结构体字段的转换
            s.sensor_id = item.value("id", "");
            s.slave_addr = (uint8_t)item.value("modbus_addr", 1);
            s.temp_register = (uint16_t)item.value("temp_register", 0);
            s.humidity_register = (uint16_t)item.value("humidity_register", 1);
            
            config_.tempHumiditySensor.sensors.push_back(s);
        }
    }
}

// ---------------- 门锁配置 ----------------
void ConfigParser::parseDoorLocks(const json& j)
{
    if (!j.contains("door_lock")) return;

    for (auto& item : j["door_lock"]) {
        DoorLockConfig d;
        d.id = item.value("id", "");
        d.name = item.value("name", "");
        d.type = item.value("type", "");
        d.gpioPin = item.value("gpio_pin", 0);
        d.description = item.value("description", "");

        config_.doorLocks.push_back(d);
    }
}