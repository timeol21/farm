#pragma once

struct Command;

class SecurityService
{

public:

    bool initialize();

    bool check(const Command& command);


};