#include "Config.h"
#include <fstream>
#include <stdexcept>

// 构造：加载并解析JSON
Config::Config(const string& filePath) {
    if (!loadJson(filePath)) {
        throw runtime_error("加载 config.json 失败");
    }
}

// 从文件加载JSON
bool Config::loadJson(const string& filePath) {
    ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    root = json::parse(ifs);//json::parse(...) = JSON 解析函数, root = 存解析后 JSON 数据的根对象（你自己定义的变量）
    return true;
}

// 工具："0x01" → unsigned char
static unsigned char hexToByte(const string& s) {
    return stoi(s, nullptr, 16);
}

// 核心函数：获取指令（你最需要的）
vector<unsigned char> Config::findPlcCommand(
    const string& plcName,
    const string& componentName,
    const string& cmdType
) {
    vector<unsigned char> buf;

    // 层级定位：plc_devices → xxx → components → xxx → commands → xxx
    auto& cmdStrArray = root["plc_devices"][plcName]["components"][componentName]["commands"][cmdType];

    for (auto& s : cmdStrArray) {
        buf.push_back(hexToByte(s));
    }

    return buf;
}