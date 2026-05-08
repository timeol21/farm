#include "common/config/json_util.h"
#include <fstream>
#include <iostream>


bool JsonUtil::readJsonFile(const std::string& filePath, json& jsonData){
    try{
        std::ifstream jsonFile(filePath);
        if(!jsonFile.is_open()){
            //日志
            return false;
        }
        jsonFile >> jsonData;
        return true;
    }catch(const std::exception& e){

        return false;

    }
}


bool JsonUtil::writeJsonFile(const std::string& filePath, const json& jsonData , bool indent){
    try{
        std::ofstream jsonFile(filePath);
        if(!jsonFile.is_open()){
            //日志
            return false;
        }
        if(indent){
            jsonFile << jsonData.dump(4); // 4 spaces for indentation
        }else{
            jsonFile << jsonData.dump();
        }
        return true;

    }catch(const std::exception& e){
        //日志
        return false;
    }
}