#pragma once
#include "business_layer/unclog/unclog_service.h"
class ILobbyService{
public:
    virtual ~ILobbyService() = default;
    
    

};

class LobbyService : public ILobbyService, public ILifecycle{
public:
    LobbyService(IUnclogService& unclogService);
    ~LobbyService() ;  

    void start() override;

   void stop() override;
    
private:

    IUnclogService& unclogService_;

};

