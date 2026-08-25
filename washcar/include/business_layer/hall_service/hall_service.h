#pragma once

#include <memory>

class CarWashState;

class CommandService;

class CommandDispatcher;

class WashService;

class SecurityService;

class AIService;

class BusinessTimer;

class DeviceManager;

class LayerBufferQueue;

class HallService
{

public:

    explicit HallService(LayerBufferQueue& bufferQueue);

    ~HallService();

    bool initialize();

    bool start();

    void stop();

    CarWashState& carWashState();

    CommandService& commandService();

    CommandDispatcher& commandDispatcher();

    WashService& washService();

    SecurityService& securityService();

    AIService& aiService();

    BusinessTimer& timer();

    DeviceManager& deviceManager();


private:

    LayerBufferQueue& bufferQueue_;

    std::unique_ptr<CarWashState> carWashState_;

    std::unique_ptr<DeviceManager> deviceManager_;

    std::unique_ptr<CommandService> commandService_;

    std::unique_ptr<CommandDispatcher> commandDispatcher_;

    std::unique_ptr<WashService> washService_;

    std::unique_ptr<SecurityService> securityService_;

    std::unique_ptr<AIService> aiService_;

    std::unique_ptr<BusinessTimer> timer_;


};