#include <iostream>
#include "business_layer/lobby/lobby_service.h"
#include "presentation_layer/http_service.h"
int main(){

    LobbyService lobbyService;
    std::cout << "Creating lobby..." << std::endl;

     HTTPCommandController controller = HTTPCommandController(lobbyService); //不允许使用抽象类类型 "IController" 的对象:

    
    WebService webService("0.0.0.0", 8080, controller); // const char [8]

    webService.start();

    while (true) {} 
    
    return 0;
};