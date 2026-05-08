#ifndef PLCCONFIG_H
#define PLCCONFIG_H

#include <string>
#include <vector>
#include <fstream>
#include <cerrno>
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

struct PLCInfo {
    int plc_id;
    string serial_port;
    string description;
};

//配置解析工具类：所有函数静态，仅暴露解析接口
class PlcConfigParser {
private:
    // 私有辅助函数：校验PLC配置有效性（外部无需调用，封装）
    static bool isValidPlcConfig(const PLCInfo& plc) {
        return plc.plc_id > 0 && !plc.serial_port.empty();
    }

public:
    // 公有接口：解析JSON配置（外部仅需调用此函数）
    static vector<PLCInfo> readPlcConfig(const string& json_path);
};

#endif