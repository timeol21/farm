// CMakeProject1.cpp: 定义应用程序的入口点。
//

#include "CMakeProject1.h"
#include "logger/logger.h"
#include "service/hall_service.h"
#include "config/config.h"
#include "config/config_parser.h"
#include "mqtt/mqtt_client.h"
using namespace std;

int main()
{
    if(!ConfigParser::getInstance().loadConfig(CONFIG_JSON)){
        LOG_ERROR(std::string("[Farming] Failed to load config: ") + CONFIG_JSON);
        return -1;
    }
    auto& mqtt = MqttClient::getInstance();

    HallService hall;
    mqtt.setHallService(&hall);
    hall.setMqttClient(&mqtt);

    mqtt.connect();

    hall.start();

    // SerialConnector serial("/dev/ttyACM0", 115200);
    // serial.open();
    // serial.send("0x01 0x10 0x00 0x3B 0x00 0x01 0x02 0x00 0x04 0xA3 0x18");
    // std::cout<<"Send"<<std::endl;
    // while(1){
    //     string response = serial.recv(2000);
    //     // printf("Received from serial: %s\n", response.c_str());
    // }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    mqtt.disconnect();
    hall.stop();

	return 0;
}
