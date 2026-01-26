#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <thread>
#include "json.hpp"
#include "DeviceFactory.h"

int main() {
    std::ifstream file("devices.json");
    if (!file) {
        std::cerr << "Cannot find devices.json" << std::endl;
        return 1;
    }

    nlohmann::json config;      //第三方 JSON 解析库，专门用于 JSON 数据
    file >> config;             //将文件流中的 JSON 数据读取并解析到 config 对象中

    std::vector<std::unique_ptr<Device>> devices;
    for (const auto& item : config) {
        auto dev = DeviceFactory::createFromJson(item);
        if (dev) devices.push_back(std::move(dev));
    }

    while (true) {
        for (const auto& dev : devices) {
            dev->execute();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}