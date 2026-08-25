#pragma once
#include <memory>
class DeviceManager;
class DatabaseManager;

class DataLayer
{

public:

    DataLayer();

    ~DataLayer();
    
    bool initialize();

    DeviceManager& deviceManager();

    DatabaseManager& databaseManager();


private:
    
    std::unique_ptr<DeviceManager> deviceManager_;

    std::unique_ptr<DatabaseManager> databaseManager_;

};