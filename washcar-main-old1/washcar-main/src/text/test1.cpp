#include <iostream>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
int main(){

    
        /// 1. 底层服务
    SafetyService safetyService;
    Timer timer;

    // 2. CommandService 构造时只依赖 ICommandPublisher 接口
    ICommandPublisher* publisher = nullptr;  // 先不绑定
    CommandService commandService(publisher);

    // 3. LobbyService
    LobbyService lobbyService(safetyService, commandService, timer);

    // 4. Controller
    MQTTCommandController mqttController(lobbyService);
    HTTPCommandController httpController(lobbyService);

    // 5. Network 层
    MqttService mqttService("192.168.1.104", 1950, mqttController);

    // 6. 现在 CommandService 可以接收 MqttService 作为 ICommandPublisher
    // 这里仍然是构造器注入，但通过接口，不破坏对象构造顺序
    commandService.setPublisher(&mqttService);  // 这里是接口注入，不是 setter逻辑

    WebService webService("192.168.1.104", 8080, httpController);

    // 7. 启动服务
    mqttService.start();
    webService.start();
    
    
    return 0;
};