#include "DeviceFactory.h"
#include "Sensors.h"
#include "Controllers.h"

std::unique_ptr<Device> DeviceFactory::createFromJson(const nlohmann::json& j) {
    try {
        std::string kind = j.at("kind");
        std::string id = j.at("id");
        std::string name = j.at("name");
        std::string port = j.at("port");

        if (kind == "Smoke") return std::unique_ptr<Device>(new SmokeSensor(id, name, port));
        if (kind == "Water") return std::unique_ptr<Device>(new WaterSensor(id, name, port));
        if (kind == "Humiture") return std::unique_ptr<Device>(new HumitureSensor(id, name, port));
        if (kind == "Valve") {
            int addr = j.value("addr", 0);
            return std::unique_ptr<Device>(new ValveController(id, name, port, addr));
        }
    } catch (...) {
        std::cerr << "JSON Parsing Error!" << std::endl;
    }
    return nullptr;
}