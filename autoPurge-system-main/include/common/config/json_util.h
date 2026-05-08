#pragma once


#include <string>
#include <mutex>
#include <climits> 
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
using json = nlohmann::json;

class JsonUtil{
public:

    static bool readJsonFile(const std::string& filePath, json& jsonData);


    static bool writeJsonFile(const std::string& filePath, const json& jsonData , bool indent = true);

private:


};