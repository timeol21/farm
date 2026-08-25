#pragma once

#include "common/message/command_message.h"

class CommandService;

class WashService;

class SecurityService;

class AIService;


class CommandDispatcher
{

public:

    CommandDispatcher(
        CommandService& commandService,
        WashService& washService,
        SecurityService& securityService,
        AIService& aiService
    );


    bool start();

    void stop();


    void dispatch();


private:

    void handleCommand(
        const CommandMessage& message
    );


private:

    CommandService& commandService_;

    WashService& washService_;

    SecurityService& securityService_;

    AIService& aiService_;

    bool running_ = false;


};