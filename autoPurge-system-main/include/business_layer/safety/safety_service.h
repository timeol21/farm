#pragma once
#include "common/application/lifecycle.h"
class ISafetyService{
public:


    virtual ~ISafetyService() = default;
    
    
    virtual bool authenticate() = 0;

    virtual bool authorize() = 0;

};


class SafetyService : public ISafetyService, public ILifecycle{
public:

    SafetyService();


    ~SafetyService();

    void start() override;

    void stop() override;
    

    bool authenticate() override;
    
    
    bool authorize() override;
    
    

};