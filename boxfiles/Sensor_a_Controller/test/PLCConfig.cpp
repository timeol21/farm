#include "PLCConfig.h"
#include <iostream>

// 实现唯一公有解析接口
vector<PLCInfo> PlcConfigParser::readPlcConfig(const string& json_path) {
    vector<PLCInfo> plc_info_list;
    ifstream json_file(json_path);

    // 检查文件打开
    if (!json_file.is_open()) {
        cerr << "错误：无法打开配置文件 " << json_path << "，原因：" << strerror(errno) << endl;
        return plc_info_list;
    }

    // 解析JSON
    json json_data;
    try {
        json_file >> json_data;
    } catch (const json::parse_error& e) {
        cerr << "错误：JSON文件解析失败！格式错误/编码问题" << endl;
        cerr << "错误详情：" << e.what() << endl;
        json_file.close();
        return plc_info_list;
    }
    json_file.close();

    // 校验plc_list节点
    if (!json_data.contains("plc_list") || !json_data["plc_list"].is_array()) {
        cerr << "错误：JSON文件中没有「plc_list」数组节点，请检查配置格式！" << endl;
        return plc_info_list;
    }

    // 遍历解析PLC配置
    for (const auto& plc_item : json_data["plc_list"]) {
        PLCInfo plc_info;
        plc_info.plc_id = plc_item.value("plc_id", 0);
        plc_info.serial_port = plc_item.value("serial_port", "");
        plc_info.description = plc_item.value("description", "无备注");
        
        // 校验有效性
        if (!isValidPlcConfig(plc_info)) {
            cerr << "警告：PLC编号" << plc_info.plc_id << " 配置无效（串口为空/编号为0），跳过！" << endl;
            continue;
        }
        plc_info_list.push_back(plc_info);
    }

    cout << "配置文件解析成功，共读取 " << plc_info_list.size() << " 个有效PLC配置！" << endl;
    return plc_info_list;
}