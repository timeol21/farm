#pragma once
#include <string>
#include <sstream>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "json.hpp"

// 支持格式: http://ip:port/path
class HttpClient {
public:
    static std::string postJson(const std::string& url, const nlohmann::json& body);

    static std::string get(const std::string& url);
        

private:
    static std::string httpRequest(const std::string& method,const std::string& url,const std::string& body);
   
    static bool parseUrl(const std::string& url,std::string& host,int& port,std::string& path);
    
};
