#pragma once

class NetworkService {
public:
    ~NetworkService() = default;

    virtual bool  start() = 0;

    virtual void stop() = 0;

    virtual void publish(const std::string& topic,const std::string& payload) = 0;
      
    virtual void subscribe(const std::string& topic) = 0;


};