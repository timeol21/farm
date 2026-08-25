#pragma once

#include <string>


class DatabaseManager
{

public:

    DatabaseManager();

    ~DatabaseManager();

    bool initialize();

    bool connect();

    void disconnect();

    bool isConnected() const;


private:


    DatabaseManager(const DatabaseManager&) = delete;
    
    DatabaseManager& operator=(const DatabaseManager&) = delete;


private:

    bool connected_ = false;

};