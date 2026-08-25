#include <iostream>
#include "config_parser.h"

void printLine() {
    std::cout << "---------------------------------------------\n";
}

int main() {
    ConfigParser::getInstance().loadFromFile("config.json");

    const auto& cfg = ConfigParser::getInstance().getConfig();

    printLine();
    std::cout << "Config Version: " << cfg.version << "\n";
    std::cout << "Description   : " << cfg.description << "\n";
    printLine();


    // ---------------- Cameras ----------------
    std::cout << "Cameras (" << cfg.cameras.size() << "):\n";
    for (const auto& cam : cfg.cameras) {
        std::cout << "  ID   : " << cam.id << "\n";
        std::cout << "  Name : " << cam.name << "\n";
        std::cout << "  URL  : " << cam.url << "\n";
        printLine();
    }


    // ---------------- PLC 本体列表 plc_list ----------------
    std::cout << "PLC List (" << cfg.plcs.size() << "):\n";
    for (const auto& plc : cfg.plcs) {
        std::cout << "PLC ID   : " << plc.plcId << "\n";
        std::cout << "Name     : " << plc.name << "\n";
        std::cout << "Type     : " << plc.connectionType << "\n";

        if (plc.hasSerial) {
            std::cout << "  --- Direct (Serial) ---\n";
            std::cout << "      Port      : " << plc.serialConfig.serial.port << "\n";
            std::cout << "      BaudRate  : " << plc.serialConfig.serial.baudRate << "\n";
            std::cout << "      Parity    : " << plc.serialConfig.serial.parity << "\n";
            std::cout << "      StopBits  : " << plc.serialConfig.serial.stopBits << "\n";
        }

        if (plc.hasGateway) {
            std::cout << "  --- Gateway ---\n";
            std::cout << "      Gateway ID : " << plc.gatewayConfig.gatewayId << "\n";
            std::cout << "      IP         : " << plc.gatewayConfig.gatewayIp << "\n";
            std::cout << "      Port       : " << plc.gatewayConfig.gatewayPort << "\n";
        }

        printLine();
    }


    // ---------------- PLC 下挂设备 plc_device ----------------
    std::cout << "PLC Devices (" << cfg.plcDevices.size() << "):\n";
    for (const auto& dev : cfg.plcDevices) {
        std::cout << "  Device ID   : " << dev.id << "\n";
        std::cout << "  Name        : " << dev.name << "\n";
        std::cout << "  PLC Owner   : " << dev.plcId << "\n";
        std::cout << "  Register    : " << dev.registerAddress << "\n";
        printLine();
    }


    // ---------------- Sensors ----------------
    std::cout << "Sensors (" << cfg.sensors.size() << "):\n";
    for (const auto& s : cfg.sensors) {
        std::cout << "  ID   : " << s.id << "\n";
        std::cout << "  Name : " << s.name << "\n";
        std::cout << "  Type : " << s.type << "\n";
        std::cout << "    Serial Port : " << s.serial.port << "\n";
        std::cout << "    Baud Rate   : " << s.serial.baudRate << "\n";
        std::cout << "    Parity      : " << s.serial.parity << "\n";
        std::cout << "    Stop Bits   : " << s.serial.stopBits << "\n";
        printLine();
    }


    // ---------------- Gateways ----------------
    std::cout << "Gateways (" << cfg.gateways.size() << "):\n";
    for (const auto& g : cfg.gateways) {
        std::cout << "  ID       : " << g.id << "\n";
        std::cout << "  Name     : " << g.name << "\n";
        std::cout << "  Model    : " << g.model << "\n";
        std::cout << "  IP       : " << g.ip << "\n";
        std::cout << "  Protocol : " << g.protocol << "\n";
        std::cout << "  Status   : " << g.status << "\n";
        printLine();
    }

    std::cout << "All config printed successfully.\n";
    return 0;
}
