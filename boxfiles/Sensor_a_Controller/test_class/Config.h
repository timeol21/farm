#ifndef GONFIG_H
#define GONFIG_H

#include <nlohmann/json.hpp>
using json = nlohmann::json;
using namespace std;

class Config{
    private:
        json root;
        bool loadJson(const string& filePath);
    public:
        Config(const string& filePath);

        vector<unsigned char> findPlcCommand(
            const string& plcName, 
            const string& indirectDeviceName,
            const string& cmdType
        );
};

#endif